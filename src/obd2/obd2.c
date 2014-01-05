#include <obd2/obd2.h>
#include <bitfield/bitfield.h>
#include <string.h>
#include <limits.h>
#include <stddef.h>
#include <sys/param.h>

#define ARBITRATION_ID_OFFSET 0x8
#define MODE_RESPONSE_OFFSET 0x40
#define NEGATIVE_RESPONSE_MODE 0x7f
#define MAX_DIAGNOSTIC_PAYLOAD_SIZE 6
#define MODE_BYTE_INDEX 0
#define PID_BYTE_INDEX 1
#define NEGATIVE_RESPONSE_MODE_INDEX 1
#define NEGATIVE_RESPONSE_NRC_INDEX 2

DiagnosticShims diagnostic_init_shims(LogShim log,
        SendCanMessageShim send_can_message,
        SetTimerShim set_timer) {
    DiagnosticShims shims = {
        log: log,
        send_can_message: send_can_message,
        set_timer: set_timer
    };
    return shims;
}

DiagnosticRequestHandle diagnostic_request(DiagnosticShims* shims,
        DiagnosticRequest* request, DiagnosticResponseReceived callback) {
    DiagnosticRequestHandle handle = {
        request: *request,
        callback: callback,
        success: false,
        completed: false
    };

    uint8_t payload[MAX_DIAGNOSTIC_PAYLOAD_SIZE];
    payload[MODE_BYTE_INDEX] = request->mode;
    if(request->pid_length > 0) {
        // TODO need a set bit field that's more natural and checks endianness
        // becauase we DO need to flip it
        copy_bytes_right_aligned((uint8_t*)&request->pid, sizeof(request->pid),
                PID_BYTE_INDEX, request->pid_length, payload, sizeof(payload));
    }
    if(request->payload_length > 0) {
        memcpy(&payload[PID_BYTE_INDEX + request->pid_length],
                request->payload, request->payload_length);
    }

    handle.isotp_shims = isotp_init_shims(shims->log,
            shims->send_can_message,
            shims->set_timer);

    handle.isotp_send_handle = isotp_send(&handle.isotp_shims,
            request->arbitration_id, payload,
            1 + request->payload_length + request->pid_length,
            NULL);
    if(shims->log != NULL) {
        shims->log("Sending diagnostic request: arb_id: 0x%02x, mode: 0x%x, pid: 0x%x, payload: 0x%02x%02x%02x%02x%02x%02x%02x%02x, size: %d\r\n",
                request->arbitration_id,
                request->mode,
                request->pid,
                request->payload[0],
                request->payload[1],
                request->payload[2],
                request->payload[3],
                request->payload[4],
                request->payload[5],
                request->payload[6],
                request->payload[7],
                request->payload_length);
    }

    handle.isotp_receive_handle = isotp_receive(&handle.isotp_shims,
            request->arbitration_id + ARBITRATION_ID_OFFSET,
            NULL);

    // TODO notes on multi frame:
    // TODO what are the timers for exactly?
    //
    // when sending multi frame, send 1 frame, wait for a response
    // if it says send all, send all right away
    // if it says flow control, set the time for the next send
    // instead of creating a timer with an async callback, add a process_handle
    // function that's called repeatedly in the main loop - if it's time to
    // send, we do it. so there's a process_handle_send and receive_can_frame
    // that are just called continuously from the main loop. it's a waste of a
    // few cpu cycles but it may be more  natural than callbacks.
    //
    // what woudl a timer callback look like...it would need to pass the handle
    // and that's all. seems like a context void* would be able to capture all
    // of the information but arg, memory allocation. look at how it's done in
    // the other library again
    //
    return handle;
}

DiagnosticRequestHandle diagnostic_request_pid(DiagnosticShims* shims,
        DiagnosticPidRequestType pid_request_type, uint16_t arbitration_id,
        uint16_t pid, DiagnosticResponseReceived callback) {
    DiagnosticRequest request = {
        arbitration_id: arbitration_id,
        mode: pid_request_type == DIAGNOSTIC_STANDARD_PID ? 0x1 : 0x22,
        pid: pid,
        pid_length: pid_request_type == DIAGNOSTIC_STANDARD_PID ? 1 : 2
    };

    return diagnostic_request(shims, &request, callback);
}

static bool handle_negative_response(IsoTpMessage* message,
        DiagnosticResponse* response, DiagnosticShims* shims) {
    bool response_was_negative = false;
    if(response->mode == NEGATIVE_RESPONSE_MODE) {
        response_was_negative = true;
        if(message->size > NEGATIVE_RESPONSE_MODE_INDEX) {
            response->mode = message->payload[NEGATIVE_RESPONSE_MODE_INDEX];
        }

        if(message->size > NEGATIVE_RESPONSE_NRC_INDEX) {
            response->negative_response_code = message->payload[NEGATIVE_RESPONSE_NRC_INDEX];
        }

        response->success = false;
        response->completed = true;
    }
    return response_was_negative;
}

static bool handle_positive_response(DiagnosticRequestHandle* handle,
        IsoTpMessage* message, DiagnosticResponse* response,
        DiagnosticShims* shims) {
    bool response_was_positive = false;
    if(response->mode == handle->request.mode + MODE_RESPONSE_OFFSET) {
        response_was_positive = true;
        // hide the "response" version of the mode from the user
        // if it matched
        response->mode = handle->request.mode;
        if(handle->request.pid_length > 0 && message->size > 1) {
            if(handle->request.pid_length == 2) {
                response->pid = *(uint16_t*)&message->payload[PID_BYTE_INDEX];
                if(BYTE_ORDER == LITTLE_ENDIAN) {
                    response->pid = __builtin_bswap32(response->pid << 16);
                }
            } else {
                response->pid = message->payload[PID_BYTE_INDEX];
            }
        }

        uint8_t payload_index = 1 + handle->request.pid_length;
        response->payload_length = message->size - payload_index;
        if(response->payload_length > 0) {
            memcpy(response->payload, &message->payload[payload_index],
                    response->payload_length);
        }
        response->success = true;
        response->completed = true;
    }
    return response_was_positive;
}

DiagnosticResponse diagnostic_receive_can_frame(DiagnosticShims* shims,
        DiagnosticRequestHandle* handle, const uint16_t arbitration_id,
        const uint8_t data[], const uint8_t size) {

    DiagnosticResponse response = {
        arbitration_id: arbitration_id,
        success: false,
        completed: false
    };

    if(!handle->isotp_send_handle.completed) {
        isotp_continue_send(&handle->isotp_shims,
                &handle->isotp_send_handle, arbitration_id, data, size);
    } else if(!handle->isotp_receive_handle.completed) {
        IsoTpMessage message = isotp_continue_receive(&handle->isotp_shims,
                &handle->isotp_receive_handle, arbitration_id, data, size);

        if(message.completed) {
            if(message.size > 0) {
                response.mode = message.payload[0];
                if(handle_negative_response(&message, &response, shims)) {
                    shims->log("Received a negative response to mode %d on arb ID 0x%x",
                            response.mode, response.arbitration_id);
                    handle->success = true;
                    handle->completed = true;
                } else if(handle_positive_response(handle, &message, &response,
                            shims)) {
                    shims->log("Received a positive mode %d response on arb ID 0x%x",
                            response.mode, response.arbitration_id);
                    handle->success = true;
                    handle->completed = true;
                } else {
                    shims->log("Response was for a mode 0x%x request, not our mode 0x%x request",
                            response.mode - MODE_RESPONSE_OFFSET,
                            handle->request.mode);
                }
            }

            if(handle->completed && handle->callback != NULL) {
                handle->callback(&response);
            }
        }

    } else {
        shims->log("Mode %d request to arb ID 0x%x is already completed",
                handle->request.mode, handle->request.arbitration_id);
    }
    return response;
}
