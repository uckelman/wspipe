#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/lexical_cast.hpp>

#include <libwebsockets.h>

#define THROW_RUNTIME(msg) \
  throw std::runtime_error( \
    static_cast<const std::ostringstream&>( \
      std::ostringstream() << __FILE__ << ":" << __LINE__ << ": " << msg \
    ).str() \
  )

#define THROW_RUNTIME_IF(condition, msg) \
  if (condition) THROW_RUNTIME(msg)


struct cbdata {
  std::string host, path;
  uint16_t port; 
  bool was_closed;
  std::vector<uint8_t> buf;
  uint8_t* cur;
};

int ws_callback(
  libwebsocket_context* ctx,
  libwebsocket* ws,
  libwebsocket_callback_reasons reason,
  void*,
  void*,
  size_t
)
{
  switch (reason) {
  case LWS_CALLBACK_CLIENT_ESTABLISHED:
    {
      cbdata& d = *static_cast<cbdata*>(libwebsocket_context_user(ctx));
      std::cerr << "Connected to websocket at "
                << d.host << ':' << d.port << d.path << std::endl;
      // schedule first write callback
      libwebsocket_callback_on_writable(ctx, ws);
    }
    break;
  case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    {
      cbdata& d = *static_cast<cbdata*>(libwebsocket_context_user(ctx));
      THROW_RUNTIME(
        "Failed to connect to websocket at " << d.host
                                             << ':' << d.port << d.path
      );
    }
  case LWS_CALLBACK_CLOSED:
    static_cast<cbdata*>(libwebsocket_context_user(ctx))->was_closed = true;
    break;
  case LWS_CALLBACK_CLIENT_WRITEABLE:
    {
      cbdata& d = *static_cast<cbdata*>(libwebsocket_context_user(ctx));

      if (!d.cur) {
        // previous line complete; pop a new line
        std::string line;
        if (std::getline(std::cin, line)) {
          line += '\n';

          // copy new line into send buffer
          d.buf.resize(
            LWS_SEND_BUFFER_PRE_PADDING +
            line.size() +
            LWS_SEND_BUFFER_POST_PADDING
          );

          d.cur = &d.buf[LWS_SEND_BUFFER_PRE_PADDING];
          std::copy(line.begin(), line.end(), d.cur);
        }
        else {
          d.was_closed = true;
          return 0;
        }
      }

      // write send buffer to websocket
      const size_t len = &*d.buf.end() - d.cur - LWS_SEND_BUFFER_POST_PADDING;

      const int sent = libwebsocket_write(ws, d.cur, len, LWS_WRITE_TEXT);
      THROW_RUNTIME_IF(
        sent < 0,
        "websocket write failed on " << d.host << ':' << d.port << d.path
      );

      if (size_t(sent) < len) {
        d.cur += sent;
      }
      else {
        d.cur = nullptr;
      }

      // schedule next write callback
      libwebsocket_callback_on_writable(ctx, ws);
    }
    break;
  default:
    break;
  }

  return 0;
}

void logger(int level, const char* line) {
  switch (level) {
  case LLL_ERR:
    THROW_RUNTIME("libwebsockets: " << line);
  case LLL_WARN:
  default:
    std::cerr << "libwebsockets: " << line << std::endl;
    break;
  }
}

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "Usage: wspipe HOST PORT PATH" << std::endl;
    return 1;
  }

  const char* host = argv[1];
  const uint16_t port = boost::lexical_cast<uint16_t>(argv[2]);
  const char* path = argv[3];

  std::cin.exceptions(std::ios_base::badbit);

  lws_set_log_level(LLL_ERR | LLL_WARN, logger); 

  cbdata d{host, path, port, false, {}, nullptr};

  libwebsocket_protocols protocols[] = {
    { "wspipe", ws_callback, 0, 0, 0, nullptr, 0 },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
  };

  lws_context_creation_info info;
  std::memset(&info, 0, sizeof(info));
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = protocols;
  info.gid = -1;
  info.uid = -1;
  info.user = &d;

  std::unique_ptr<libwebsocket_context, void(*)(libwebsocket_context*)> ctx{
    libwebsocket_create_context(&info),
    libwebsocket_context_destroy
  };

  THROW_RUNTIME_IF(
    !ctx,
    "Creating libwebsocket context failed for " << host << ':' << port << path
  );

  libwebsocket* ws = libwebsocket_client_connect(
    ctx.get(),
    host,
    port,
    0,    // SSL
    path,
    host,
    host,
    protocols[0].name,
    -1 // latest protocol version
  );

  THROW_RUNTIME_IF(
    !ws,
    "libwebsocket connect failed for " << host << ':' << port << path
  );

  std::cerr << "Waiting for connection to "
            << host << ':' << port << path << std::endl;
  while (libwebsocket_service(ctx.get(), 0) >= 0 && !d.was_closed);
  std::cerr << "Disconnected from "
            << host << ':' << port << path << std::endl;
  return 0;
}
