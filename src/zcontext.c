#include "zcontext.h"
#include "lzutils.h"
#include "lzmq.h"
#include <assert.h>

int luazmq_context_create (lua_State *L) {
  zcontext *zctx = luazmq_newudata(L, zcontext, LUAZMQ_CONTEXT);
  zctx->ctx = zmq_ctx_new();
  zctx->autoclose_ref = LUA_NOREF;

#ifdef LZMQ_DEBUG
  zctx->socket_count = 0;
#endif

  return 1;
}

int luazmq_context_init (lua_State *L) {
  zcontext *zctx = luazmq_newudata(L, zcontext, LUAZMQ_CONTEXT);
  int n = luaL_optint(L, 1, 1);
  zctx->ctx = zmq_init(n);
  zctx->autoclose_ref = LUA_NOREF;

#ifdef LZMQ_DEBUG
  zctx->socket_count = 0;
#endif

  return 1;
}

int luazmq_init_ctx (lua_State *L) {
  zcontext *src_ctx = (zcontext *)lua_touserdata(L,1);
  if(src_ctx){
    zcontext *zctx = luazmq_newudata(L, zcontext, LUAZMQ_CONTEXT);
    zctx->ctx = src_ctx->ctx;
    zctx->flags = LUAZMQ_FLAG_DONT_DESTROY;
    zctx->autoclose_ref = LUA_NOREF;

#ifdef LZMQ_DEBUG
  zctx->socket_count = 0;
#endif

    return 1;
  }
  luaL_argcheck(L, 0, 1, "lightuserdata expected");
  return 0;
}

static int luazmq_ctx_lightuserdata(lua_State *L) {
  zcontext *zctx = luazmq_getcontext(L);
  lua_pushlightuserdata(L, zctx);
  return 1;
}

static int luazmq_ctx_tostring(lua_State *L) {
  zcontext *ctx = luazmq_getcontext(L);
  lua_pushstring(L, LUAZMQ_CONTEXT);
  return 1;
}

static int luazmq_ctx_set (lua_State *L) {
  zcontext *ctx = luazmq_getcontext(L);
  int option_name  = luaL_checkint(L, 2);
  int option_value = luaL_checkint(L, 3);
  int ret = zmq_ctx_set(ctx->ctx,option_name,option_value);
  if (ret == -1) return luazmq_fail(L,NULL);
  return luazmq_pass(L);
}

static int luazmq_ctx_get (lua_State *L) {
  zcontext *ctx = luazmq_getcontext(L);
  int option_name  = luaL_checkint(L, 2);
  int ret = zmq_ctx_get(ctx->ctx,option_name);
  if (ret == -1) return luazmq_fail(L,NULL);
  lua_pushinteger(L, ret);
  return 1;
}

static int luazmq_ctx_autoclose (lua_State *L) {
  zcontext *ctx = luazmq_getcontext(L);
  zsocket  *skt = luazmq_getsocket_at(L,2);

  lua_settop(L, 2);

  if(LUA_NOREF == ctx->autoclose_ref){
    lua_newtable(L);
    ctx->autoclose_ref = luaL_ref(L, lua_upvalueindex(1));
  }
  lua_rawgeti(L, lua_upvalueindex(1), ctx->autoclose_ref);
  lua_insert(L,2);
  lua_rawseti(L, 2, lua_rawlen(L, 2) + 1);
  return 0;
}

static int luazmq_ctx_close_sockets (lua_State *L, zcontext *ctx) {
  size_t top = lua_gettop(L);

  if(LUA_NOREF == ctx->autoclose_ref) return 0;

  lua_rawgeti(L, lua_upvalueindex(1), ctx->autoclose_ref);
  assert(lua_istable(L, -1));
  {
  size_t n = lua_rawlen(L, -1);
  size_t i;

  for(i = 1; i<= n; ++i){
    lua_rawgeti(L, -1, i);
    { // copy from luazmq_skt_destroy
      zsocket *skt = (zsocket *)luazmq_checkudatap (L, -1, LUAZMQ_SOCKET);
      assert(skt != NULL);
      if(!(skt->flags & LUAZMQ_FLAG_CLOSED)){
        int ret;
        luazmq_skt_before_close(L, skt);

        ret = zmq_close(skt->skt);

#ifdef LZMQ_DEBUG
        assert(skt->ctx == ctx);
        skt->ctx->socket_count--;
#endif
        // what we can do with error ???
        // if(ret == -1)return luazmq_fail(L, skt);
        skt->flags |= LUAZMQ_FLAG_CLOSED;
      }
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  luaL_unref(L, lua_upvalueindex(1), ctx->autoclose_ref);
  ctx->autoclose_ref = LUA_NOREF;

  assert(top == lua_gettop(L));

  }
  return 0;
}


#ifdef LZMQ_DEBUG

static int luazmq_ctx_skt_count (lua_State *L) {
  zcontext *ctx = luazmq_getcontext(L);
  lua_pushinteger(L, ctx->socket_count);
  return 1;
}

#endif

static int luazmq_ctx_destroy (lua_State *L) {
  zcontext *ctx = (zcontext *)luazmq_checkudatap (L, 1, LUAZMQ_CONTEXT);
  luaL_argcheck (L, ctx != NULL, 1, LUAZMQ_PREFIX"context expected");
  if(!(ctx->flags & LUAZMQ_FLAG_CLOSED)){
    luazmq_ctx_close_sockets(L, ctx);
    if(!(ctx->flags & LUAZMQ_FLAG_DONT_DESTROY)){
      int ret = zmq_ctx_destroy(ctx->ctx);
      if(ret == -1)return luazmq_fail(L,NULL);
    }
    ctx->flags |= LUAZMQ_FLAG_CLOSED;
  }
  return luazmq_pass(L);
}

static int luazmq_ctx_closed (lua_State *L) {
  zcontext *ctx = (zcontext *)luazmq_checkudatap (L, 1, LUAZMQ_CONTEXT);
  luaL_argcheck (L, ctx != NULL, 1, LUAZMQ_PREFIX"context expected");
  lua_pushboolean(L, ctx->flags & LUAZMQ_FLAG_CLOSED);
  return 1;
}

static int luazmq_create_socket (lua_State *L) {
  zsocket *zskt;
  zcontext *ctx = luazmq_getcontext(L);
  int stype = luaL_checkint(L,2);
  void *skt = zmq_socket(ctx->ctx, stype);
  if(!skt)return luazmq_fail(L,NULL);

  zskt = luazmq_newudata(L, zsocket, LUAZMQ_SOCKET);
  zskt->skt = skt;
  zskt->onclose_ref = LUA_NOREF;

#ifdef LZMQ_DEBUG
  ctx->socket_count++;
  zskt->ctx = ctx;
#endif

  return 1;
}

#define DEFINE_CTX_OPT(NAME, OPTNAME) \
  static int luazmq_ctx_set_##NAME(lua_State *L){\
    lua_pushinteger(L, OPTNAME);\
    lua_insert(L, 2);\
    return luazmq_ctx_set(L);\
  }\
  static int luazmq_ctx_get_##NAME(lua_State *L){\
    lua_pushinteger(L, OPTNAME);\
    return luazmq_ctx_get(L);\
  }

#define REGISTER_CTX_OPT(NAME) {"set_"#NAME, luazmq_ctx_set_##NAME}, {"get_"#NAME, luazmq_ctx_get_##NAME}

DEFINE_CTX_OPT(io_threads,  ZMQ_IO_THREADS)
DEFINE_CTX_OPT(max_sockets, ZMQ_MAX_SOCKETS)


static const struct luaL_Reg luazmq_ctx_methods[] = {
  {"set",           luazmq_ctx_set           },
  {"get",           luazmq_ctx_get           },
  {"lightuserdata", luazmq_ctx_lightuserdata },

#ifdef LZMQ_DEBUG
  {"socket_count",  luazmq_ctx_skt_count     },
#endif

  REGISTER_CTX_OPT(io_threads),
  REGISTER_CTX_OPT(max_sockets),

  {"closed",     luazmq_ctx_closed },
  {NULL,NULL}
};
static const struct luaL_Reg luazmq_ctx_methods_2[] = {
  {"socket",     luazmq_create_socket  },
  {"autoclose",  luazmq_ctx_autoclose  },
  {"__gc",       luazmq_ctx_destroy    },
  {"destroy",    luazmq_ctx_destroy    },
  {"term",       luazmq_ctx_destroy    },
  {NULL,NULL}
};


void luazmq_context_initlib (lua_State *L){
  luazmq_createmeta(L, LUAZMQ_CONTEXT, luazmq_ctx_methods);
  lua_newtable(L);
  luazmq_setfuncs(L, luazmq_ctx_methods_2, 1);
  lua_pop(L, 1);
}
