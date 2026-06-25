/* tcpCat — libuv TCP client: connect, write, read until EOF. */

#include <assert.h>
#include <bare.h>
#include <js.h>
#include <stdlib.h>
#include <string.h>
#include <utf.h>
#include <uv.h>

#define TCP_CAT_INITIAL_CAPACITY 4096

typedef struct
{
  uv_tcp_t handle;
  uv_connect_t connect;
  uv_write_t write;

  js_env_t *env;
  js_deferred_t *deferred;

  char *write_data;
  size_t write_len;

  char *response;
  size_t response_len;
  size_t response_cap;

  bool handle_open;
  bool exiting;
} tcp_cat_req_t;


static void
tcp_cat__cleanup(tcp_cat_req_t *req)
{
  free(req->write_data); 
  free(req->response);
  free(req);
} 

static void
tcp_cat__on_close(uv_handle_t *handle)
{
  tcp_cat_req_t *req = handle->data;
  tcp_cat__cleanup(req);
}

static void
tcp_cat__close(tcp_cat_req_t *req)
{
  if (req->handle_open && !uv_is_closing((uv_handle_t *) &req->handle))
  {
    uv_close((uv_handle_t *) &req->handle, tcp_cat__on_close);   
  }
  else 
  {
    tcp_cat__cleanup(req);
  }
}

static int
tcp_cat__append(tcp_cat_req_t *req, const char *data, size_t len)
{
  if (len == 0)
  {
    return 0;
  }

  size_t needed = req->response_len + len;

  if (needed > req->response_cap)   
  {
    size_t cap = req->response_cap ? req->response_cap : TCP_CAT_INITIAL_CAPACITY;   

    while (cap < needed)
    {
      cap *= 2;
    }

    char *next = realloc(req->response, cap);

    if (!next)
    {
      return UV_ENOMEM;
    }

    req->response = next;
    req->response_cap = cap;
  }

  memcpy(req->response + req->response_len, data, len);
  req->response_len += len;

  return 0;
}

static void
tcp_cat__reject(tcp_cat_req_t *req, const char *code, const char *message)
{
  if (req->exiting || req->deferred == NULL)
  {
    return;
  }

  req->exiting = true;

  int err;
  js_env_t *env = req->env;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(env, &scope);
  assert(err == 0);

  js_value_t *err_code;
  err = js_create_string_utf8(env, (utf8_t *) code, -1, &err_code);
  assert(err == 0);

  js_value_t *err_message;
  err = js_create_string_utf8(env, (utf8_t *) message, -1, &err_message);
  assert(err == 0);

  js_value_t *error;
  err = js_create_error(env, err_code, err_message, &error);
  assert(err == 0);

  err = js_reject_deferred(env, req->deferred, error);
  assert(err == 0);

  req->deferred = NULL;

  err = js_close_handle_scope(env, scope);
  assert(err == 0);
}

static void
tcp_cat__resolve(tcp_cat_req_t *req)
{
  if (req->exiting || req->deferred == NULL)
  {
    return;
  }

  req->exiting = true;

  int err;
  js_env_t *env = req->env;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(env, &scope);
  assert(err == 0);

  js_value_t *result;
  void *data;

  err = js_create_arraybuffer(env, req->response_len, &data, &result);
  assert(err == 0);

  if (req->response_len > 0)
  {
    memcpy(data, req->response, req->response_len);
  }

  err = js_resolve_deferred(env, req->deferred, result);
  assert(err == 0);

  req->deferred = NULL;

  err = js_close_handle_scope(env, scope);
  assert(err == 0);
}

static void
tcp_cat__fail(tcp_cat_req_t *req, int status)
{
  tcp_cat__reject(req, uv_err_name(status), uv_strerror(status));
  tcp_cat__close(req);
}

static void
tcp_cat__finish_read(tcp_cat_req_t *req)
{
  uv_read_stop((uv_stream_t *) &req->handle);
  tcp_cat__resolve(req);
  tcp_cat__close(req);
}

static void
tcp_cat__on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
  (void) handle;

  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

static void
tcp_cat__on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
  tcp_cat_req_t *req = stream->data;

  if (nread == UV_EOF)
  {
    free(buf->base);
    /* Peer closed the connection — done. Keep-alive would not end here. */
    tcp_cat__finish_read(req);
    return;
  }

  if (nread < 0)
  {
    free(buf->base);
    tcp_cat__fail(req, nread);
    return;
  }

  if (nread == 0)
  {
    free(buf->base);
    return;
  }

  if (tcp_cat__append(req, buf->base, nread) < 0)
  {
    free(buf->base);
    tcp_cat__fail(req, UV_ENOMEM);
    return;
  }

  free(buf->base);
}

static void
tcp_cat__on_write(uv_write_t *write_req, int status)
{
  tcp_cat_req_t *req = write_req->data;

  if (status < 0)
  {
    tcp_cat__fail(req, status);
    return;
  }

  int err = uv_read_start((uv_stream_t *) &req->handle, tcp_cat__on_alloc, tcp_cat__on_read);

  if (err < 0)
  {
    tcp_cat__fail(req, err);
  }
}

static void
tcp_cat__on_connect(uv_connect_t *connect_req, int status)
{
  tcp_cat_req_t *req = connect_req->data;

  if (status < 0)
  {
    tcp_cat__fail(req, status);
    return;
  }

  uv_buf_t buf = uv_buf_init(req->write_data, req->write_len);

  req->write.data = req;

  int err = uv_write(&req->write, (uv_stream_t *) &req->handle, &buf, 1, tcp_cat__on_write);

  if (err < 0)
  {
    tcp_cat__fail(req, err);
  }
}

static int
tcp_cat__get_bytes(js_env_t *env, js_value_t *value, char **data, size_t *len)
{
  int err;
  bool is_string;

  err = js_is_string(env, value, &is_string);
  if (err < 0)
  {
    return err;
  }

  if (is_string)
  {
    size_t str_len;

    err = js_get_value_string_utf8(env, value, NULL, 0, &str_len);
    if (err < 0)
    {
      return err;
    }

    char *buf = malloc(str_len);
    if (!buf)
    {
      return UV_ENOMEM;
    }

    err = js_get_value_string_utf8(env, value, (utf8_t *) buf, str_len, NULL);
    if (err < 0)
    {
      free(buf);
      return err;
    }

    *data = buf;
    *len = str_len;
    return 0;
  }

  bool is_typedarray;

  err = js_is_typedarray(env, value, &is_typedarray);
  if (err < 0)
  {
    return err;
  }

  if (is_typedarray)
  {
    void *buf;
    size_t buf_len;

    /* Copy out — the backing store may move once we yield to the loop. */
    err = js_get_typedarray_info(env, value, NULL, &buf, &buf_len, NULL, NULL);
    if (err < 0)
    {
      return err;
    }

    char *copy = malloc(buf_len);
    if (!copy)
    {
      return UV_ENOMEM;
    }

    memcpy(copy, buf, buf_len);

    *data = copy;
    *len = buf_len;
    return 0;
  }

  return UV_EINVAL;
}

static js_value_t *
bare_addon_tcp_cat(js_env_t *env, js_callback_info_t *info)
{
  int err;

  size_t argc = 3;
  js_value_t *argv[3];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  if (err < 0)
  {
    return NULL;
  }

  assert(argc >= 3);

  utf8_t host[INET6_ADDRSTRLEN];
  err = js_get_value_string_utf8(env, argv[0], host, sizeof(host), NULL);
  if (err < 0)
  {
    return NULL;
  }

  uint32_t port;
  err = js_get_value_uint32(env, argv[1], &port);
  if (err < 0)
  {
    return NULL;
  }

  js_deferred_t *deferred;
  js_value_t *promise;

  err = js_create_promise(env, &deferred, &promise);
  if (err < 0)
  {
    return NULL;
  }

  tcp_cat_req_t *req = calloc(1, sizeof(tcp_cat_req_t));
  if (!req)
  {
    js_value_t *message;
    err = js_create_string_utf8(env, (utf8_t *) "Out of memory", -1, &message);
    assert(err == 0);

    js_value_t *error;
    err = js_create_error(env, NULL, message, &error);
    assert(err == 0);

    err = js_reject_deferred(env, deferred, error);
    assert(err == 0);

    return promise;
  }

  req->env = env;
  req->deferred = deferred;

  err = tcp_cat__get_bytes(env, argv[2], &req->write_data, &req->write_len);
  if (err < 0)
  {
    tcp_cat__reject(req, uv_err_name(err), uv_strerror(err));
    tcp_cat__cleanup(req);
    return promise;
  }

  uv_loop_t *loop;
  err = js_get_env_loop(env, &loop);
  if (err < 0)
  {
    tcp_cat__reject(req, "EINVAL", "Failed to get event loop");
    tcp_cat__cleanup(req);
    return promise;
  }

  req->handle.data = req;

  err = uv_tcp_init(loop, &req->handle);
  if (err < 0)
  {
    tcp_cat__reject(req, uv_err_name(err), uv_strerror(err));
    tcp_cat__cleanup(req);
    return promise;
  }

  req->handle_open = true;

  struct sockaddr_storage addr;

  if (strchr((char *) host, ':') != NULL)
  {
    err = uv_ip6_addr((char *) host, port, (struct sockaddr_in6 *) &addr);
  }
  else
  {
    err = uv_ip4_addr((char *) host, port, (struct sockaddr_in *) &addr);
  }

  if (err < 0)
  {
    tcp_cat__reject(req, uv_err_name(err), uv_strerror(err));
    tcp_cat__close(req);
    return promise;
  }

  req->connect.data = req;

  err = uv_tcp_connect(&req->connect, &req->handle, (struct sockaddr *) &addr, tcp_cat__on_connect);
  if (err < 0)
  {
    tcp_cat__reject(req, uv_err_name(err), uv_strerror(err));
    tcp_cat__close(req);
    return promise;
  }

  return promise;
}

static js_value_t *
bare_addon_exports(js_env_t *env, js_value_t *exports)
{
  int err;

#define V(name, fn) \
  { \
    js_value_t *val; \
    err = js_create_function(env, name, -1, fn, NULL, &val); \
    assert(err == 0); \
    err = js_set_named_property(env, exports, name, val); \
    assert(err == 0); \
  }

  V("tcpCat", bare_addon_tcp_cat)
#undef V

  return exports;
}

BARE_MODULE(bare_addon, bare_addon_exports)
