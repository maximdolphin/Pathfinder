# Driving the editor over MCP

The project ships the **MCP Automation Bridge** plugin, which lets an AI client
control the Unreal editor — spawn actors, run Python, issue console commands,
inspect the level. It was configured but not working; this is what it needed.

## What was wrong

`.mcp.json` pointed at `http://localhost:3000/mcp` and the session started with
`ConnectionRefused`. Three separate reasons, each of which looked like the
whole problem until it was fixed:

**1. Nothing was serving port 3000.** The plugin has two transports. The
WebSocket bridge listens on 8090/8091 and starts by default — but it does not
speak MCP. The native MCP Streamable HTTP server is the one that does, and
`bEnableNativeMCP` defaults to **false**. It is a `config=Game` setting, so it
belongs in `client/Config/DefaultGame.ini`.

**2. The editor has to be running.** This is an editor plugin; the server lives
inside the editor process. Nothing serves that port when the editor is closed,
which is why the connection failed at session start and would fail again on any
session started before opening the project.

**3. The token goes in a header nobody would guess.** Capability-token auth is
on by default. It is not `Authorization: Bearer` — the parser looks for
`X-MCP-Capability-Token`, and anything else comes back as
`{"error":{"code":-32600,"message":"Invalid capability token"}}`, which reads
like a wrong token rather than a wrong header.

## How it is set up now

`client/Config/DefaultGame.ini` enables the native server on port 3000, bound to
loopback, with the capability token required.

`.mcp.json` refers to `${UNREAL_MCP_TOKEN}` rather than carrying the token,
because that file is committed and **this repository is public**. The token
authorises running arbitrary Python inside the editor; it stays in
`client/Saved/MCP/capability-token`, which is gitignored.

```bash
python tools/mcp_token.py          # prints the line that sets the variable
python tools/mcp_token.py --check  # is the endpoint up and the token accepted
```

`--check` initialises a real MCP session, so a pass means the endpoint, the
token and the editor are all working — not merely that a port is open.

## Using it

1. Open the project in the Unreal editor and leave it open.
2. `python tools/mcp_token.py --check` should print `VERDICT: PASS`.
3. Start Claude Code. The MCP client connects at startup, so the editor must
   already be running.

The server exposes exactly one tool, `unreal`, with four operations — `search`,
`describe`, `execute`, `configure` — over about 1,400 editor capabilities. The
intended order is search, then describe the exact capability, then execute.

## Verified working

```
initialize   -> unreal-mcp 0.5.30, protocol 2025-06-18
tools/list   -> 1 tool: unreal
search       -> control_actor.list, inspect.list_objects, ...
execute      -> control_actor.list -> "Actors listed", status success
```

Each request needs three headers: `X-MCP-Capability-Token`, `Accept:
application/json, text/event-stream`, and `Mcp-Session-Id` from the
`initialize` response — a call without the session id is rejected with
`Missing Mcp-Session-Id header`.

## What is still not available

**Computer use.** Its MCP server disconnected earlier in the session and its
tools are gone from this one. Nothing in the project can bring it back; it is a
client-side server, restored by restarting the client.
