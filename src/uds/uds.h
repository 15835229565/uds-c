#ifndef __UDS_H__
#define __UDS_H__

#include <uds/uds_types.h>
#include <stdint.h>
#include <stdbool.h>

#define OBD2_FUNCTIONAL_BROADCAST_ID 0x7df
#define OBD2_FUNCTIONAL_RESPONSE_START 0x7e8
#define OBD2_FUNCTIONAL_RESPONSE_COUNT 8

#ifdef __cplusplus
extern "C" {
#endif

/* Public: Initialize an DiagnosticShims with the given callback functions.
 *
 * If any callbacks are not to be used, set them to NULL. For documentation of
 * the function type signatures, see higher up in this header file. This struct
 * is a handy encapsulation used to pass the shims around to the various
 * diagnostic_* functions.
 *
 * Returns a struct with the fields initailized to the callbacks.
 */
DiagnosticShims diagnostic_init_shims(LogShim log,
        SendCanMessageShim send_can_message,
        SetTimerShim set_timer);

/* Public: Initiate a diagnostic request and return a handle, ready to completly
 * send the request and process the response via
 * diagnostic_receive_can_frame(...).
 *
 * shims -  Low-level shims required to send CAN messages, etc.
 * request -
 * callback - an optional function to be called when the response is receved
 *      (use NULL if no callback is required).
 *
 * Returns a handle to be used with diagnostic_receive_can_frame to complete
 * sending the request and receive the response. The 'completed' field in the
 * returned DiagnosticRequestHandle will be true when the message is completely
 * sent.
 */
DiagnosticRequestHandle diagnostic_request(DiagnosticShims* shims,
        DiagnosticRequest* request, DiagnosticResponseReceived callback);

/* Public: Request a PID from the given arbitration ID, determining the mode
 * automatically based on the PID type.
 *
 * shims -  Low-level shims required to send CAN messages, etc.
 * pid_request_type - either DIAGNOSTIC_STANDARD_PID (will use mode 0x1 and 1
 *      byte PIDs) or DIAGNOSTIC_ENHANCED_PID (will use mode 0x22 and 2 byte
 *      PIDs)
 * arbitration_id - The arbitration ID to send the request to.
 * pid - The PID to request from the other node.
 * callback - an optional function to be called when the response is receved
 *      (use NULL if no callback is required).
 *
 * Returns a handle to be used with diagnostic_receive_can_frame to complete
 * sending the request and receive the response. The 'completed' field in the
 * returned DiagnosticRequestHandle will be true when the message is completely
 * sent.
 */
DiagnosticRequestHandle diagnostic_request_pid(DiagnosticShims* shims,
        DiagnosticPidRequestType pid_request_type, uint16_t arbitration_id,
        uint16_t pid, DiagnosticResponseReceived callback);

/* Public: Continue to send and receive a single diagnostic request, based on a
 * freshly received CAN message.
 *
 * shims -  Low-level shims required to send CAN messages, etc.
 * handle - A DiagnosticRequestHandle previously returned by one of the
 *      diagnostic_request*(..) functions.
 * arbitration_id - The arbitration_id of the received CAN message.
 * data - The data of the received CAN message.
 * size - The size of the data in the received CAN message.
 *
 * Returns true if the request was completed and response received, or the
 * request was otherwise cancelled. Check the 'success' field of the handle to
 * see if it was successful.
 */
DiagnosticResponse diagnostic_receive_can_frame(DiagnosticShims* shims,
        DiagnosticRequestHandle* handle,
        const uint16_t arbitration_id, const uint8_t data[],
        const uint8_t size);

float diagnostic_payload_to_float(const DiagnosticResponse* response);

/* Public: Render a DiagnosticResponse as a string into the given buffer.
 *
 * TODO implement this
 *
 * message - the response to convert to a string, for debug logging.
 * destination - the target string buffer.
 * destination_length - the size of the destination buffer, i.e. the max size
 *      for the rendered string.
 */
// void diagnostic_response_to_string(const DiagnosticResponse* response,
        // char* destination, size_t destination_length);

#ifdef __cplusplus
}
#endif

#endif // __UDS_H__
