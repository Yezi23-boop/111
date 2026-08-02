import fs from "node:fs";
import crypto from "node:crypto";
import { URL } from "node:url";
import { authorize } from "./auth.js";
import { MusicEngine } from "./engine.js";
import { loadConfig } from "./config.js";
import { MusicStore } from "./store.js";
import { NeteaseProvider } from "./netease_provider.js";

const MAX_BODY_BYTES = 64 * 1024;

function json(response, status, body) {
  const payload = JSON.stringify(body);
  response.statusCode = status;
  response.setHeader("Content-Type", "application/json; charset=utf-8");
  response.setHeader("Cache-Control", "no-store");
  response.end(payload);
}

async function readJson(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > MAX_BODY_BYTES) throw new Error("body_too_large");
    chunks.push(chunk);
  }
  if (size === 0) return {};
  return JSON.parse(Buffer.concat(chunks).toString("utf8"));
}

function deviceIdFrom(url, body = {}) {
  return url.searchParams.get("device_id") || body.device_id || "";
}

function commandId(request, body) {
  return request.headers["x-command-id"] || body.command_id || "";
}

function requireAuth(request, deviceId, config, response) {
  const result = authorize(request, deviceId, config);
  if (!result.ok) {
    json(response, result.status, { state: "error", error_code: result.errorCode });
    return false;
  }
  return true;
}

function sources(config) {
  if (!config.testMode) return null;
  return [
    {
      source_id: "test",
      title: "固定测试流",
      kind: "test",
      track_count: 1,
    },
  ];
}

function errorCode(error) {
  return error instanceof Error && error.message
    ? error.message
    : "music_upstream_unavailable";
}

export function createApp(overrides = {}) {
  const config = loadConfig(overrides);
  fs.mkdirSync(config.dataDir, { recursive: true });
  const store = new MusicStore(config.dbPath);
  const engine = new MusicEngine(store, config);
  const provider = overrides.provider || new NeteaseProvider(config);

  async function ensureAccount() {
    if (!provider.hasCookie()) return null;
    const stored = store.getAccount();
    if (stored) return stored;
    const account = await provider.account();
    if (account) store.saveAccount(account);
    return account;
  }

  const server = async (request, response) => {
    const url = new URL(request.url, `http://${request.headers.host || "localhost"}`);
    if (url.pathname === "/health" && request.method === "GET") {
      return json(response, 200, { status: "ok", service: "music-service" });
    }
    if (!url.pathname.startsWith("/v1/music")) return json(response, 404, { error_code: "not_found" });

    let body = {};
    if (["POST", "DELETE"].includes(request.method)) {
      try {
        body = await readJson(request);
      } catch {
        return json(response, 400, { state: "error", error_code: "invalid_json" });
      }
    }
    const deviceId = deviceIdFrom(url, body);
    if (!requireAuth(request, deviceId, config, response)) return;

    if (url.pathname === "/v1/music/account" && request.method === "GET") {
      if (config.testMode) return json(response, 200, { state: "logged_out" });
      try {
        const account = await ensureAccount();
        return json(response, 200, account ? { state: "logged_in", account } : { state: "logged_out" });
      } catch (error) {
        if (errorCode(error) === "account_expired") {
          provider.clearCookie();
          store.clearAccount();
          return json(response, 200, { state: "expired" });
        }
        return json(response, 503, { state: "error", error_code: errorCode(error) });
      }
    }
    if (url.pathname === "/v1/music/account/qr" && request.method === "POST") {
      if (config.testMode) return json(response, 409, { state: "error", error_code: "music_auth_unavailable" });
      try {
        const qr = await provider.createQr();
        const loginId = `login-${crypto.randomUUID()}`;
        store.saveQr(loginId, qr.key, qr.expires_at);
        return json(response, 200, {
          state: "qr_pending", login_id: loginId, expires_at: qr.expires_at, qr: qr.modules,
        });
      } catch (error) {
        return json(response, 503, { state: "error", error_code: errorCode(error) });
      }
    }
    const qrMatch = url.pathname.match(/^\/v1\/music\/account\/qr\/([^/]+)$/);
    if (qrMatch && request.method === "GET") {
      const qr = store.getQr(decodeURIComponent(qrMatch[1]));
      if (!qr || qr.expires_at <= Date.now()) {
        return json(response, 410, { state: "expired", error_code: "qr_expired" });
      }
      try {
        const result = await provider.checkQr(qr.qr_key);
        if (result.state === "logged_in" && result.account) store.saveAccount(result.account);
        return json(response, 200, result);
      } catch (error) {
        if (errorCode(error) === "account_expired") {
          provider.clearCookie();
          store.clearAccount();
          return json(response, 200, { state: "expired" });
        }
        return json(response, 503, { state: "error", error_code: errorCode(error) });
      }
    }
    if (url.pathname === "/v1/music/account" && request.method === "DELETE") {
      if (body.confirm !== true) {
        return json(response, 400, { state: "error", error_code: "confirmation_required" });
      }
      engine.destroy(deviceId, commandId(request, body));
      provider.clearCookie();
      store.clearAccount();
      return json(response, 200, { state: "logged_out" });
    }
    if (url.pathname.startsWith("/v1/music/sources") && request.method === "GET") {
      const availableSources = sources(config);
      if (availableSources && url.pathname !== "/v1/music/sources") {
        const tracks = [{ track_id: "test-track", title: "固定测试流", artist: "AI Memory Watch" }];
        return json(response, 200, { state: "ready", source_id: "test", tracks });
      }
      if (availableSources) return json(response, 200, { state: "ready", sources: availableSources });
      let account;
      try {
        account = await ensureAccount();
      } catch (error) {
        if (errorCode(error) === "account_expired") {
          provider.clearCookie();
          store.clearAccount();
          return json(response, 401, { state: "error", error_code: "music_auth_expired" });
        }
        return json(response, 503, { state: "error", error_code: errorCode(error) });
      }
      if (!account) return json(response, 401, { state: "error", error_code: "music_auth_required" });
      try {
        if (url.pathname === "/v1/music/sources") {
          return json(response, 200, { state: "ready", ...(await provider.sources(account)) });
        }
        const sourceId = decodeURIComponent(url.pathname.slice("/v1/music/sources/".length).replace(/\/tracks$/, ""));
        const offset = Math.max(0, Number(url.searchParams.get("offset") || 0));
        const limit = Math.min(10, Math.max(1, Number(url.searchParams.get("limit") || 10)));
        return json(response, 200, { state: "ready", source_id: sourceId, ...(await provider.tracks(sourceId, account, offset, limit)) });
      } catch (error) {
        if (errorCode(error) === "account_expired") {
          provider.clearCookie();
          store.clearAccount();
          return json(response, 401, { state: "error", error_code: "music_auth_expired" });
        }
        return json(response, 503, { state: "error", error_code: errorCode(error) });
      }
    }
    if (url.pathname === "/v1/music/sessions" && request.method === "POST") {
      const currentCommand = commandId(request, body);
      if (config.testMode && body.source_id === "test") {
        const result = engine.createTestSession(deviceId, currentCommand);
        return json(response, result.state === "error" ? 409 : 200, result);
      }
      let account;
      try {
        account = await ensureAccount();
      } catch (error) {
        if (errorCode(error) === "account_expired") {
          provider.clearCookie();
          store.clearAccount();
          return json(response, 401, { state: "error", error_code: "music_auth_expired" });
        }
        return json(response, 503, { state: "error", error_code: errorCode(error) });
      }
      if (!account) return json(response, 401, { state: "error", error_code: "music_auth_required" });
      const duplicate = engine.getCommand(deviceId, currentCommand);
      if (duplicate) return json(response, duplicate.state === "error" ? 409 : 200, duplicate);
      try {
        const definition = await provider.playback(body.source_id, body.track_id || null, account);
        const result = engine.createSession(deviceId, currentCommand, definition);
        return json(response, result.state === "error" ? 409 : 200, result);
      } catch (error) {
        if (errorCode(error) === "account_expired") {
          provider.clearCookie();
          store.clearAccount();
          return json(response, 401, { state: "error", error_code: "music_auth_expired" });
        }
        return json(response, 409, { state: "error", error_code: errorCode(error) });
      }
    }

    const sessionMatch = url.pathname.match(/^\/v1\/music\/sessions\/([^/]+)(?:\/(pause|resume|previous|next|mode))?$/);
    if (sessionMatch) {
      const sessionId = sessionMatch[1];
      const session = engine.getSession(deviceId);
      if (!session || session.music_session_id !== sessionId) {
        return json(response, 404, { state: "error", error_code: "music_session_not_found" });
      }
      const action = sessionMatch[2];
      let result;
      const resolveTrack = config.testMode ? null : provider.resolveTrack?.bind(provider);
      if (request.method === "DELETE" && !action) result = engine.destroy(deviceId, commandId(request, body));
      else if (request.method === "POST" && action === "pause") result = engine.pause(deviceId, commandId(request, body));
      else if (request.method === "POST" && action === "resume") result = await engine.resume(deviceId, commandId(request, body), resolveTrack);
      else if (request.method === "POST" && action === "previous") result = await engine.previous(deviceId, commandId(request, body), resolveTrack);
      else if (request.method === "POST" && action === "next") result = await engine.next(deviceId, commandId(request, body), resolveTrack);
      else if (request.method === "POST" && action === "mode") result = engine.setMode(deviceId, commandId(request, body), body.mode);
      else return json(response, 405, { state: "error", error_code: "method_not_allowed" });
      return json(response, result.state === "error" ? 409 : 200, result);
    }

    const streamMatch = url.pathname.match(/^\/v1\/music\/streams\/([^/]+)$/);
    if (streamMatch && request.method === "GET") {
      response.statusCode = 200;
      response.setHeader("Content-Type", "audio/mpeg");
      response.setHeader("Cache-Control", "no-store");
      response.setHeader("Transfer-Encoding", "chunked");
      const result = engine.attachStream(deviceId, streamMatch[1], response);
      if (result.state === "error") {
        response.removeHeader("Content-Type");
        response.removeHeader("Cache-Control");
        response.removeHeader("Transfer-Encoding");
        return json(response, 409, result);
      }
      return;
    }
    return json(response, 404, { state: "error", error_code: "not_found" });
  };

  server.closeMusic = () => {
    engine.close();
    store.close();
  };
  server.config = config;
  server.store = store;
  server.engine = engine;
  server.provider = provider;
  return server;
}
