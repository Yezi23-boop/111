# Memory Watch Feature Boundary

This directory owns user-visible Memory Watch product semantics, including
foreground waiting, detached replies, conversation notifications, and inbox
distinction.

The current behavior remains implemented by the existing controller and
`services/memory_watch` runtime. A feature facade should only be added when it
can remove product-policy decisions from UI or service code; this directory
must not contain HTTP, WebSocket, recorder, NVS, or LVGL implementation details.
