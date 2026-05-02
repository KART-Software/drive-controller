#ifndef _COMMAND_ROUTER_H_
#define _COMMAND_ROUTER_H_

#include "proto/drive_controller.pb.h"

#define MAX_ROUTES 24

// `which_body` is the dc_Command oneof tag identifying the sub-message.
using CommandHandler = void (*)(void* ctx, const dc_Command& cmd);

struct Route {
    pb_size_t which_body;
    CommandHandler handler;
    void* ctx;
};

class CommandRouter {
   public:
    void on(pb_size_t which_body, CommandHandler handler, void* ctx = nullptr);
    void poll();

   private:
    Route routes[MAX_ROUTES];
    uint8_t routeCount = 0;
};

#endif
