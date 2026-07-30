"""Endpoint-side client for the private Watch Relay Connector ingress."""

from __future__ import annotations

import httpx


class RelayTransportError(RuntimeError):
    """Raised when a Relay submission is not durably accepted."""


class RelaySessionBusyError(RelayTransportError):
    """Raised when the single watch session already has an active turn."""


class RelayTransportClient:
    """Small stateless client; endpoint SQLite remains the task truth source."""

    def __init__(self, base_url: str, internal_token: str, timeout_seconds: float = 10.0) -> None:
        self._base_url = base_url.rstrip("/")
        self._internal_token = internal_token
        self._timeout = httpx.Timeout(timeout_seconds)

    @property
    def configured(self) -> bool:
        return bool(self._base_url and self._internal_token)

    async def submit_turn(self, device_id: str, request_id: str, text: str) -> dict[str, object]:
        if not self.configured:
            raise RelayTransportError("relay_transport_not_configured")
        try:
            async with httpx.AsyncClient(timeout=self._timeout, trust_env=False) as client:
                response = await client.post(
                    f"{self._base_url}/internal/relay/turn",
                    headers={"Authorization": f"Bearer {self._internal_token}"},
                    json={"device_id": device_id, "request_id": request_id, "text": text},
                )
        except httpx.RequestError as exc:
            raise RelayTransportError("relay_connector_unreachable") from exc
        if response.status_code == 409:
            try:
                detail = response.json().get("detail")
            except ValueError:
                detail = None
            if detail == "relay_turn_conflict":
                raise RelaySessionBusyError("relay_session_busy")
        if response.status_code not in (200, 202):
            raise RelayTransportError(f"relay_submit_rejected:{response.status_code}")
        payload = response.json()
        if not isinstance(payload, dict) or not payload.get("accepted"):
            raise RelayTransportError("relay_submit_not_accepted")
        return payload

    async def cancel_turn(self, device_id: str, request_id: str) -> dict[str, object]:
        if not self.configured:
            raise RelayTransportError("relay_transport_not_configured")
        try:
            async with httpx.AsyncClient(timeout=self._timeout, trust_env=False) as client:
                response = await client.post(
                    f"{self._base_url}/internal/relay/turn/{request_id}/cancel",
                    headers={"Authorization": f"Bearer {self._internal_token}"},
                    json={"device_id": device_id},
                )
        except httpx.RequestError as exc:
            raise RelayTransportError("relay_connector_unreachable") from exc
        if response.status_code not in (200, 202, 409):
            raise RelayTransportError(f"relay_cancel_rejected:{response.status_code}")
        payload = response.json()
        return payload if isinstance(payload, dict) else {}
