var __defProp = Object.defineProperty;
var __name = (target, value) => __defProp(target, "name", { value, configurable: true });

// src/worker.js
var CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
var CODE_LEN = 4;
var ROOM_TTL_MS = 2 * 60 * 60 * 1e3;
function random_code() {
  const bytes = crypto.getRandomValues(new Uint8Array(CODE_LEN));
  let code = "";
  for (const b of bytes) code += CODE_ALPHABET[b % CODE_ALPHABET.length];
  return code;
}
__name(random_code, "random_code");
var worker_default = {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname !== "/ws")
      return new Response("newtonia-signal", { status: 200 });
    if (request.headers.get("Upgrade") !== "websocket")
      return new Response("expected websocket", { status: 426 });
    const role = url.searchParams.get("role");
    if (role === "host") {
      for (let attempt = 0; attempt < 8; attempt++) {
        const code = random_code();
        const room = env.ROOMS.get(env.ROOMS.idFromName(code));
        const resp = await room.fetch(
          new Request(`https://room/host?code=${code}`, request)
        );
        if (resp.status !== 409) return resp;
      }
      return new Response("no free room codes", { status: 503 });
    }
    if (role === "join") {
      const code = (url.searchParams.get("code") || "").toUpperCase();
      if (code.length !== CODE_LEN || [...code].some((c) => !CODE_ALPHABET.includes(c)))
        return new Response("bad code", { status: 400 });
      const room = env.ROOMS.get(env.ROOMS.idFromName(code));
      return room.fetch(new Request(`https://room/join?code=${code}`, request));
    }
    return new Response("bad role", { status: 400 });
  }
};
var Room = class {
  static {
    __name(this, "Room");
  }
  constructor(state) {
    this.state = state;
    this.host = null;
    this.joiner = null;
    this.offer = null;
    this.created = 0;
  }
  async fetch(request) {
    const url = new URL(request.url);
    const code = url.searchParams.get("code");
    const now = Date.now();
    if (this.host && now - this.created > ROOM_TTL_MS) this.expire();
    if (url.pathname === "/host") {
      if (this.host) return new Response("room in use", { status: 409 });
      const pair = new WebSocketPair();
      this.accept_host(pair[1], code, now);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }
    if (url.pathname === "/join") {
      if (!this.host) return this.reject_ws("no-such-room");
      if (this.joiner) return this.reject_ws("room-full");
      const pair = new WebSocketPair();
      this.accept_joiner(pair[1]);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }
    return new Response("not found", { status: 404 });
  }
  // Refusals still complete the WS upgrade so the client gets a readable
  // {t:"err"} frame instead of an opaque HTTP error it may not surface.
  reject_ws(reason) {
    const pair = new WebSocketPair();
    pair[1].accept();
    pair[1].send(JSON.stringify({ t: "err", reason }));
    pair[1].close(1e3);
    return new Response(null, { status: 101, webSocket: pair[0] });
  }
  accept_host(ws, code, now) {
    ws.accept();
    this.host = ws;
    this.offer = null;
    this.created = now;
    ws.send(JSON.stringify({ t: "room", code }));
    ws.addEventListener("message", (m) => this.from_host(m));
    const drop = /* @__PURE__ */ __name(() => this.expire(), "drop");
    ws.addEventListener("close", drop);
    ws.addEventListener("error", drop);
  }
  accept_joiner(ws) {
    ws.accept();
    this.joiner = ws;
    ws.send(JSON.stringify({ t: "joined" }));
    if (this.host) this.host.send(JSON.stringify({ t: "peer", ev: "join" }));
    if (this.offer) ws.send(JSON.stringify({ t: "offer", sdp: this.offer }));
    ws.addEventListener("message", (m) => this.from_joiner(m));
    const drop = /* @__PURE__ */ __name(() => this.drop_joiner(), "drop");
    ws.addEventListener("close", drop);
    ws.addEventListener("error", drop);
  }
  from_host(m) {
    let msg;
    try {
      msg = JSON.parse(m.data);
    } catch (e) {
      return;
    }
    if (msg.t === "offer" && typeof msg.sdp === "string") {
      this.offer = msg.sdp;
      if (this.joiner) this.joiner.send(JSON.stringify(msg));
    }
  }
  from_joiner(m) {
    let msg;
    try {
      msg = JSON.parse(m.data);
    } catch (e) {
      return;
    }
    if (msg.t === "answer" && typeof msg.sdp === "string") {
      if (this.host) this.host.send(JSON.stringify(msg));
    }
  }
  drop_joiner() {
    this.joiner = null;
    if (this.host) this.host.send(JSON.stringify({ t: "peer", ev: "leave" }));
  }
  expire() {
    const bye = JSON.stringify({ t: "err", reason: "expired" });
    for (const ws of [this.host, this.joiner]) {
      if (!ws) continue;
      try {
        ws.send(bye);
        ws.close(1e3);
      } catch (e) {
      }
    }
    this.host = null;
    this.joiner = null;
    this.offer = null;
  }
};

// ../../../../root/.npm/_npx/d77349f55c2be1c0/node_modules/wrangler/templates/middleware/middleware-ensure-req-body-drained.ts
var drainBody = /* @__PURE__ */ __name(async (request, env, _ctx, middlewareCtx) => {
  try {
    return await middlewareCtx.next(request, env);
  } finally {
    try {
      if (request.body !== null && !request.bodyUsed) {
        const reader = request.body.getReader();
        while (!(await reader.read()).done) {
        }
      }
    } catch (e) {
      console.error("Failed to drain the unused request body.", e);
    }
  }
}, "drainBody");
var middleware_ensure_req_body_drained_default = drainBody;

// ../../../../root/.npm/_npx/d77349f55c2be1c0/node_modules/wrangler/templates/middleware/middleware-miniflare3-json-error.ts
function reduceError(e) {
  return {
    name: e?.name,
    message: e?.message ?? String(e),
    stack: e?.stack,
    cause: e?.cause === void 0 ? void 0 : reduceError(e.cause)
  };
}
__name(reduceError, "reduceError");
var jsonError = /* @__PURE__ */ __name(async (request, env, _ctx, middlewareCtx) => {
  try {
    return await middlewareCtx.next(request, env);
  } catch (e) {
    const error = reduceError(e);
    return Response.json(error, {
      status: 500,
      headers: { "MF-Experimental-Error-Stack": "true" }
    });
  }
}, "jsonError");
var middleware_miniflare3_json_error_default = jsonError;

// .wrangler/tmp/bundle-QczxjA/middleware-insertion-facade.js
var __INTERNAL_WRANGLER_MIDDLEWARE__ = [
  middleware_ensure_req_body_drained_default,
  middleware_miniflare3_json_error_default
];
var middleware_insertion_facade_default = worker_default;

// ../../../../root/.npm/_npx/d77349f55c2be1c0/node_modules/wrangler/templates/middleware/common.ts
var __facade_middleware__ = [];
function __facade_register__(...args) {
  __facade_middleware__.push(...args.flat());
}
__name(__facade_register__, "__facade_register__");
function __facade_invokeChain__(request, env, ctx, dispatch, middlewareChain) {
  const [head, ...tail] = middlewareChain;
  const middlewareCtx = {
    dispatch,
    next(newRequest, newEnv) {
      return __facade_invokeChain__(newRequest, newEnv, ctx, dispatch, tail);
    }
  };
  return head(request, env, ctx, middlewareCtx);
}
__name(__facade_invokeChain__, "__facade_invokeChain__");
function __facade_invoke__(request, env, ctx, dispatch, finalMiddleware) {
  return __facade_invokeChain__(request, env, ctx, dispatch, [
    ...__facade_middleware__,
    finalMiddleware
  ]);
}
__name(__facade_invoke__, "__facade_invoke__");

// .wrangler/tmp/bundle-QczxjA/middleware-loader.entry.ts
var __Facade_ScheduledController__ = class ___Facade_ScheduledController__ {
  constructor(scheduledTime, cron, noRetry) {
    this.scheduledTime = scheduledTime;
    this.cron = cron;
    this.#noRetry = noRetry;
  }
  scheduledTime;
  cron;
  static {
    __name(this, "__Facade_ScheduledController__");
  }
  #noRetry;
  noRetry() {
    if (!(this instanceof ___Facade_ScheduledController__)) {
      throw new TypeError("Illegal invocation");
    }
    this.#noRetry();
  }
};
function wrapExportedHandler(worker) {
  if (__INTERNAL_WRANGLER_MIDDLEWARE__ === void 0 || __INTERNAL_WRANGLER_MIDDLEWARE__.length === 0) {
    return worker;
  }
  for (const middleware of __INTERNAL_WRANGLER_MIDDLEWARE__) {
    __facade_register__(middleware);
  }
  const fetchDispatcher = /* @__PURE__ */ __name(function(request, env, ctx) {
    if (worker.fetch === void 0) {
      throw new Error("Handler does not export a fetch() function.");
    }
    return worker.fetch(request, env, ctx);
  }, "fetchDispatcher");
  return {
    ...worker,
    fetch(request, env, ctx) {
      const dispatcher = /* @__PURE__ */ __name(function(type, init) {
        if (type === "scheduled" && worker.scheduled !== void 0) {
          const controller = new __Facade_ScheduledController__(
            Date.now(),
            init.cron ?? "",
            () => {
            }
          );
          return worker.scheduled(controller, env, ctx);
        }
      }, "dispatcher");
      return __facade_invoke__(request, env, ctx, dispatcher, fetchDispatcher);
    }
  };
}
__name(wrapExportedHandler, "wrapExportedHandler");
function wrapWorkerEntrypoint(klass) {
  if (__INTERNAL_WRANGLER_MIDDLEWARE__ === void 0 || __INTERNAL_WRANGLER_MIDDLEWARE__.length === 0) {
    return klass;
  }
  for (const middleware of __INTERNAL_WRANGLER_MIDDLEWARE__) {
    __facade_register__(middleware);
  }
  return class extends klass {
    #fetchDispatcher = /* @__PURE__ */ __name((request, env, ctx) => {
      this.env = env;
      this.ctx = ctx;
      if (super.fetch === void 0) {
        throw new Error("Entrypoint class does not define a fetch() function.");
      }
      return super.fetch(request);
    }, "#fetchDispatcher");
    #dispatcher = /* @__PURE__ */ __name((type, init) => {
      if (type === "scheduled" && super.scheduled !== void 0) {
        const controller = new __Facade_ScheduledController__(
          Date.now(),
          init.cron ?? "",
          () => {
          }
        );
        return super.scheduled(controller);
      }
    }, "#dispatcher");
    fetch(request) {
      return __facade_invoke__(
        request,
        this.env,
        this.ctx,
        this.#dispatcher,
        this.#fetchDispatcher
      );
    }
  };
}
__name(wrapWorkerEntrypoint, "wrapWorkerEntrypoint");
var WRAPPED_ENTRY;
if (typeof middleware_insertion_facade_default === "object") {
  WRAPPED_ENTRY = wrapExportedHandler(middleware_insertion_facade_default);
} else if (typeof middleware_insertion_facade_default === "function") {
  WRAPPED_ENTRY = wrapWorkerEntrypoint(middleware_insertion_facade_default);
}
var middleware_loader_entry_default = WRAPPED_ENTRY;
export {
  Room,
  __INTERNAL_WRANGLER_MIDDLEWARE__,
  middleware_loader_entry_default as default
};
//# sourceMappingURL=worker.js.map
