import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createServer } from "node:http";
import { PassThrough } from "node:stream";
import test from "node:test";
import { createApp } from "../src/app.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const fixture = path.resolve(
  here,
  "../../../managed_components/chmorgan__esp-audio-player/test/gs-16b-1c-44100hz.mp3",
);

async function runningApp(overrides = {}) {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), "watch-music-"));
  const app = createApp({
    dataDir,
    dbPath: path.join(dataDir, "music.db"),
    testMode: true,
    testStreamPath: fixture,
    deviceTokens: new Map([["watch-001", "test-token"]]),
    ...overrides,
  });
  const server = createServer(app);
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const address = server.address();
  const base = `http://127.0.0.1:${address.port}`;
  return {
    app,
    base,
    close: async () => {
      app.closeMusic();
      await new Promise((resolve) => server.close(resolve));
      fs.rmSync(dataDir, { recursive: true, force: true });
    },
  };
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function headers(commandId) {
  return {
    Authorization: "Bearer test-token",
    "Content-Type": "application/json",
    ...(commandId ? { "X-Command-Id": commandId } : {}),
  };
}

test("music endpoints independently authenticate device token", async () => {
  const running = await runningApp();
  try {
    const missing = await fetch(`${running.base}/v1/music/sources?device_id=watch-001`);
    assert.equal(missing.status, 401);
    const wrong = await fetch(`${running.base}/v1/music/sources?device_id=watch-001`, {
      headers: { Authorization: "Bearer wrong" },
    });
    assert.equal(wrong.status, 403);
    const ok = await fetch(`${running.base}/v1/music/sources?device_id=watch-001`, {
      headers: { Authorization: "Bearer test-token" },
    });
    assert.equal(ok.status, 200);
    assert.equal((await ok.json()).sources[0].source_id, "test");
  } finally {
    await running.close();
  }
});

test("session commands are idempotent and pause/resume preserves session", async () => {
  const running = await runningApp();
  try {
    const create = await fetch(`${running.base}/v1/music/sessions`, {
      method: "POST",
      headers: headers("create-1"),
      body: JSON.stringify({ device_id: "watch-001", source_id: "test", track_id: "test-track" }),
    });
    assert.equal(create.status, 200);
    const first = await create.json();
    const duplicate = await fetch(`${running.base}/v1/music/sessions`, {
      method: "POST",
      headers: headers("create-1"),
      body: JSON.stringify({ device_id: "watch-001", source_id: "test", track_id: "test-track" }),
    });
    assert.deepEqual(await duplicate.json(), first);

    const pause = await fetch(
      `${running.base}/v1/music/sessions/${first.music_session_id}/pause?device_id=watch-001`,
      { method: "POST", headers: headers("pause-1"), body: "{}" },
    );
    assert.equal((await pause.json()).state, "paused");
    const resume = await fetch(
      `${running.base}/v1/music/sessions/${first.music_session_id}/resume?device_id=watch-001`,
      { method: "POST", headers: headers("resume-1"), body: "{}" },
    );
    const resumed = await resume.json();
    assert.equal(resumed.state, "buffering");
    assert.notEqual(resumed.stream_id, first.stream_id);
  } finally {
    await running.close();
  }
});

test("stream endpoint returns chunked audio/mpeg without spawning duplicate stream", async () => {
  const running = await runningApp();
  try {
    const create = await fetch(`${running.base}/v1/music/sessions?device_id=watch-001`, {
      method: "POST",
      headers: headers("create-stream"),
      body: JSON.stringify({ source_id: "test", track_id: "test-track" }),
    });
    const session = await create.json();
    const stream = await fetch(
      `${running.base}/v1/music/streams/${session.stream_id}?device_id=watch-001`,
      { headers: { Authorization: "Bearer test-token" } },
    );
    assert.equal(stream.status, 200);
    assert.equal(stream.headers.get("content-type"), "audio/mpeg");
    const reader = stream.body.getReader();
    const first = await reader.read();
    assert.ok(first.value.byteLength > 0);
    await reader.cancel();
  } finally {
    await running.close();
  }
});

test("pausing an attached stream closes the HTTP response", async () => {
  const running = await runningApp();
  try {
    const create = await fetch(`${running.base}/v1/music/sessions?device_id=watch-001`, {
      method: "POST",
      headers: headers("pause-stream-create"),
      body: JSON.stringify({ source_id: "test", track_id: "test-track" }),
    });
    const session = await create.json();
    const stream = await fetch(
      `${running.base}/v1/music/streams/${session.stream_id}?device_id=watch-001`,
      { headers: { Authorization: "Bearer test-token" } },
    );
    const reader = stream.body.getReader();
    const first = await reader.read();
    assert.ok(first.value.byteLength > 0);

    const pause = await fetch(
      `${running.base}/v1/music/sessions/${session.music_session_id}/pause?device_id=watch-001`,
      { method: "POST", headers: headers("pause-stream-command"), body: "{}" },
    );
    assert.equal((await pause.json()).state, "paused");

    const stopped = await Promise.race([
      reader.read().then(() => ({ closed: true })).catch(() => ({ closed: true })),
      delay(1000).then(() => ({ timeout: true })),
    ]);
    assert.notEqual(stopped.timeout, true, "paused stream did not close");
    assert.equal(stopped.closed, true);
  } finally {
    await running.close();
  }
});

test("disconnected stream can reattach during grace and is then reclaimed", async () => {
  const running = await runningApp({ streamDisconnectCleanupMs: 80 });
  try {
    const session = running.app.engine.createTestSession("watch-001", "direct-1");
    const firstResponse = new PassThrough();
    assert.equal(
      running.app.engine.attachStream(
        "watch-001",
        session.stream_id,
        firstResponse,
      ).state,
      "streaming",
    );
    await new Promise((resolve) => firstResponse.once("data", resolve));
    firstResponse.destroy();
    await delay(10);

    const secondResponse = new PassThrough();
    assert.equal(
      running.app.engine.attachStream(
        "watch-001",
        session.stream_id,
        secondResponse,
      ).state,
      "streaming",
    );
    secondResponse.destroy();
    await delay(130);
    assert.equal(running.app.engine.child, null);
    assert.equal(running.app.engine.getSession("watch-001").state, "paused");
  } finally {
    await running.close();
  }
});

test("private Netease adapter exposes QR, source summaries, and no upstream URL", async () => {
  let loggedIn = false;
  const provider = {
    hasCookie: () => loggedIn,
    createQr: async () => ({ key: "private-key", expires_at: Date.now() + 120_000, modules: { size: 21, data: "AA==" } }),
    checkQr: async () => {
      loggedIn = true;
      return { state: "logged_in", account: { uid: "42", nickname: "测试用户" } };
    },
    clearCookie: () => { loggedIn = false; },
    sources: async () => ({
      sources: [{ source_id: "today", title: "今日推荐", kind: "tracks", track_count: 1 }],
      playlists: [],
    }),
    tracks: async () => ({ tracks: [{ track_id: "song-1", title: "测试歌曲", artist: "测试歌手" }], total: 1 }),
    playback: async (_sourceId, _trackId) => ({
      sourceId: "today",
      track: { track_id: "song-1", title: "测试歌曲", artist: "测试歌手" },
      queue: [{ track_id: "song-1", title: "测试歌曲", artist: "测试歌手" }],
      input: "https://private-upstream.invalid/audio",
    }),
  };
  const running = await runningApp({ testMode: false, provider });
  try {
    const qr = await fetch(`${running.base}/v1/music/account/qr?device_id=watch-001`, {
      method: "POST", headers: headers("qr-1"), body: "{}",
    });
    const qrPayload = await qr.json();
    assert.equal(qrPayload.state, "qr_pending");
    assert.equal(qrPayload.qr.size, 21);

    const login = await fetch(`${running.base}/v1/music/account/qr/${qrPayload.login_id}?device_id=watch-001`, {
      headers: { Authorization: "Bearer test-token" },
    });
    assert.equal((await login.json()).state, "logged_in");

    const source = await fetch(`${running.base}/v1/music/sources/today/tracks?device_id=watch-001`, {
      headers: { Authorization: "Bearer test-token" },
    });
    assert.equal((await source.json()).tracks[0].track_id, "song-1");

    const session = await fetch(`${running.base}/v1/music/sessions`, {
      method: "POST", headers: headers("real-session-1"),
      body: JSON.stringify({ device_id: "watch-001", source_id: "today", track_id: "song-1" }),
    });
    const payload = await session.json();
    assert.equal(payload.track.title, "测试歌曲");
    assert.equal(JSON.stringify(payload).includes("private-upstream"), false);

    const missingConfirm = await fetch(`${running.base}/v1/music/account?device_id=watch-001`, {
      method: "DELETE", headers: headers("logout-1"), body: "{}",
    });
    assert.equal(missingConfirm.status, 400);
    const logout = await fetch(`${running.base}/v1/music/account?device_id=watch-001`, {
      method: "DELETE", headers: headers("logout-2"), body: JSON.stringify({ confirm: true }),
    });
    assert.equal((await logout.json()).state, "logged_out");
  } finally {
    await running.close();
  }
});

test("source-only session lets the server choose the first track", async () => {
  let requestedTrackId = "unset";
  const provider = {
    hasCookie: () => true,
    sources: async () => ({ sources: [], playlists: [] }),
    playback: async (_sourceId, trackId) => {
      requestedTrackId = trackId;
      return {
        sourceId: "today",
        track: { track_id: "song-1", title: "首曲", artist: "歌手" },
        queue: [{ track_id: "song-1", title: "首曲", artist: "歌手" }],
        input: fixture,
      };
    },
  };
  const running = await runningApp({ testMode: false, provider });
  try {
    running.app.store.saveAccount({ uid: "42", nickname: "测试用户" });
    const response = await fetch(`${running.base}/v1/music/sessions`, {
      method: "POST", headers: headers("source-only-1"),
      body: JSON.stringify({ device_id: "watch-001", source_id: "today" }),
    });
    assert.equal(response.status, 200);
    assert.equal(requestedTrackId, null);
    assert.equal((await response.json()).track.track_id, "song-1");
  } finally {
    await running.close();
  }
});

test("next uses the persisted queue and resolves only the selected upstream track", async () => {
  const resolved = [];
  const provider = {
    hasCookie: () => true,
    playback: async () => ({
      sourceId: "today",
      track: { track_id: "song-1", title: "第一首", artist: "歌手" },
      queue: [
        { track_id: "song-1", title: "第一首", artist: "歌手" },
        { track_id: "song-2", title: "第二首", artist: "歌手" },
      ],
      input: fixture,
    }),
    resolveTrack: async (trackId) => {
      resolved.push(trackId);
      return fixture;
    },
  };
  const running = await runningApp({ testMode: false, provider });
  try {
    running.app.store.saveAccount({ uid: "42", nickname: "测试用户" });
    const create = await fetch(`${running.base}/v1/music/sessions`, {
      method: "POST", headers: headers("queue-create"),
      body: JSON.stringify({ device_id: "watch-001", source_id: "today" }),
    });
    const first = await create.json();
    const next = await fetch(`${running.base}/v1/music/sessions/${first.music_session_id}/next?device_id=watch-001`, {
      method: "POST", headers: headers("queue-next"), body: "{}",
    });
    const second = await next.json();
    assert.equal(second.track.track_id, "song-2");
    assert.deepEqual(resolved, ["song-2"]);
  } finally {
    await running.close();
  }
});

test("resume re-resolves the current track instead of reusing an expired URL", async () => {
  const resolved = [];
  const provider = {
    hasCookie: () => true,
    playback: async () => ({
      sourceId: "today",
      track: { track_id: "song-1", title: "第一首", artist: "歌手" },
      queue: [{ track_id: "song-1", title: "第一首", artist: "歌手" }],
      input: "old-url",
    }),
    resolveTrack: async (trackId) => {
      resolved.push(trackId);
      return "fresh-url";
    },
  };
  const running = await runningApp({ testMode: false, provider });
  try {
    running.app.store.saveAccount({ uid: "42", nickname: "测试用户" });
    const create = await fetch(`${running.base}/v1/music/sessions`, {
      method: "POST", headers: headers("resume-create"),
      body: JSON.stringify({ device_id: "watch-001", source_id: "today" }),
    });
    const first = await create.json();
    await fetch(`${running.base}/v1/music/sessions/${first.music_session_id}/pause?device_id=watch-001`, {
      method: "POST", headers: headers("resume-pause"), body: "{}",
    });
    const resume = await fetch(`${running.base}/v1/music/sessions/${first.music_session_id}/resume?device_id=watch-001`, {
      method: "POST", headers: headers("resume-resume"), body: "{}",
    });
    assert.equal((await resume.json()).state, "buffering");
    assert.deepEqual(resolved, ["song-1"]);
  } finally {
    await running.close();
  }
});

test("a persisted cookie can rehydrate the account summary after SQLite metadata loss", async () => {
  let accountCalls = 0;
  const provider = {
    hasCookie: () => true,
    account: async () => {
      accountCalls += 1;
      return { uid: "42", nickname: "重启用户" };
    },
  };
  const running = await runningApp({ testMode: false, provider });
  try {
    const response = await fetch(`${running.base}/v1/music/account?device_id=watch-001`, {
      headers: { Authorization: "Bearer test-token" },
    });
    const payload = await response.json();
    assert.equal(payload.state, "logged_in");
    assert.equal(payload.account.nickname, "重启用户");
    assert.equal(accountCalls, 1);
    assert.equal(running.app.store.getAccount().uid, "42");
  } finally {
    await running.close();
  }
});
