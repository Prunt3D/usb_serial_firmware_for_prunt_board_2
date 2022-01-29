//
// Qusb USB Device Library for libopencm3
//
// Copyright (c) 2021 Manuel Bleichenbacher
// Copyright (c) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
// Licensed under LGPL License https://opensource.org/licenses/LGPL-3.0
// Derived from libopencm3 (https://github.com/libopencm3/libopencm3)
//

//
// Code for handling control transfers (of enpoint 0).
//

#include "qusb_private.h"
#include <stdlib.h>

// Register application callback function for handling USB control requests
int qusb_dev_register_control_callback(qusb_device* dev, uint8_t type, uint8_t type_mask, qusb_dev_control_callback_fn callback)
{
    for (int i = 0; i < MAX_USER_CONTROL_CALLBACK; i++) {
        if (dev->user_control_callback[i].cb)
            continue;

        dev->user_control_callback[i].type = type;
        dev->user_control_callback[i].type_mask = type_mask;
        dev->user_control_callback[i].cb = callback;
        return 0;
    }

    return -1;
}

/**
 * Stall endpoint 0.
 *
 * @param dev USB device
 */
static void stall(qusb_device* dev)
{
    qusb_dev_ep_stall_set(dev, 0, 1);
    dev->control_state.state = IDLE;
}

/**
 * Send DATA IN packet
 *
 * @param dev USB device
 */
static void send_data_in(qusb_device* dev)
{
    uint32_t max_packet_size = dev->desc->bMaxPacketSize0;
    if (max_packet_size < dev->control_state.ctrl_len) {
        // Partial chunk
        qusb_dev_ep_write_packet(dev, 0, dev->control_state.ctrl_buf, max_packet_size);
        dev->control_state.state = DATA_IN;
        dev->control_state.ctrl_buf += max_packet_size;
        dev->control_state.ctrl_len -= max_packet_size;
        dev->control_state.req.wLength -= max_packet_size;
    } else {
        // Last data chunk and zero-length packet
        qusb_dev_ep_write_packet(dev, 0, dev->control_state.ctrl_buf, dev->control_state.ctrl_len);

        // ZLP is required if transmitted data is shorter than what was specified in the setup stage (in wLength)
        // and if the last packet is equal to the packet size (no short packet)
        if (dev->control_state.ctrl_len == max_packet_size
            && dev->control_state.ctrl_len < dev->control_state.req.wLength)
            dev->control_state.state = DATA_IN; // not done -> will result in one more data packet (ZLP)
        else
            dev->control_state.state = LAST_DATA_IN;
        dev->control_state.ctrl_len = 0;
        dev->control_state.ctrl_buf = NULL;
    }
}

/**
 * Accepts a DATA OUT packet and appends it to the control data buffer
 *
 * @param dev USB device
 */
static int read_data_out(qusb_device* dev)
{
    uint16_t packetsize
        = min_u16(dev->desc->bMaxPacketSize0, dev->control_state.req.wLength - dev->control_state.ctrl_len);
    uint16_t size = qusb_dev_ep_read_packet(dev, 0, dev->control_state.ctrl_buf + dev->control_state.ctrl_len, packetsize);

    if (size != packetsize) {
        stall(dev);
        return -1;
    }

    dev->control_state.ctrl_len += size;
    return packetsize;
}

/**
 * Dispatches control request.
 *
 * The function first checks for a matching user callback. If no user callback matches
 * or the matching callbacks don't handle it, passes it on to the standard request handler.
 *
 * @param dev USB device
 * @param req USB request (setup data)
 * @return code indicating if request has been handled
 */
static qusb_request_return_code dispatch_request(qusb_device* dev, qusb_setup_data* req)
{
    struct user_control_callback* cb_list = dev->user_control_callback;

    // call user command callbacks (if there is a match)
    for (int i = 0; i < MAX_USER_CONTROL_CALLBACK; i++) {
        if (cb_list[i].cb == NULL)
            break;

        if ((req->bmRequestType & cb_list[i].type_mask) == cb_list[i].type) {
            int result = cb_list[i].cb(dev, req, &(dev->control_state.ctrl_buf), &(dev->control_state.ctrl_len),
                &(dev->control_state.completion));
            if (result == QUSB_REQ_HANDLED || result == QUSB_REQ_NOTSUPP)
                return result;
        }
    }

    // forward to standard request if not handled by user callback
    return _qusb_standard_request(dev, req, &(dev->control_state.ctrl_buf), &(dev->control_state.ctrl_len));
}

/**
 * Handle control request (in case of no DATA OUT packets)
 *
 * @param dev USB device
 * @param req USB request (setup data)
 */
static void handle_request_no_data(qusb_device* dev, qusb_setup_data* req)
{
    // prepare buffer for response
    dev->control_state.ctrl_buf = dev->ctrl_buf;
    dev->control_state.ctrl_len = req->wLength;

    if (dispatch_request(dev, req)) {
        // successfully handled
        if (req->wLength > 0) {
            // send response as DATA IN packet(s)
            send_data_in(dev);
        } else {
            // submit STATUS IN packet (if response has no data)
            qusb_dev_ep_write_packet(dev, 0, NULL, 0);
            dev->control_state.state = STATUS_IN;
        }
    } else {
        // stall endpoint on failure
        stall(dev);
    }
}

/**
 * Prepare to receive DATA OUT packets
 *
 * @param dev USB device
 * @param req USB request (setup data)
 */
static void prepare_data_out(qusb_device* dev, qusb_setup_data* req)
{
    if (req->wLength > dev->ctrl_buf_len) {
        // host has announced a request payload bigger the control data buffer
        stall(dev);
        return;
    }

    // setup buffer for receiving control request data
    dev->control_state.ctrl_buf = dev->ctrl_buf;
    dev->control_state.ctrl_len = 0;

    // wait for DATA OUT packets
    dev->control_state.state = req->wLength > dev->desc->bMaxPacketSize0 ? DATA_OUT : LAST_DATA_OUT;
}

// Handle SETUP events of endpoint 0
void _qusb_control_setup(qusb_device* dev, __attribute__((unused)) uint8_t ep, __attribute__((unused)) uint32_t len)
{
    qusb_setup_data* req = &dev->control_state.req;

    dev->control_state.completion = NULL;

    // retrieve SETUP packet
    if (qusb_dev_ep_read_packet(dev, 0, (uint8_t*)req, 8) != 8) {
        stall(dev);
        return;
    }

    if (req->wLength == 0 || (req->bmRequestType & QUSB_REQ_TYPE_DIRECTION_MASK) == QUSB_REQ_TYPE_IN) {
        // no DATA OUT packets will arrive - process control request
        handle_request_no_data(dev, req);
    } else {
        // prepare for DATA OUT packets
        prepare_data_out(dev, req);
    }
}

// Handle CONTROL OUT events of endpoint 0
void _qusb_control_out(qusb_device* dev, __attribute__((unused)) uint8_t ep, __attribute__((unused)) uint32_t len)
{
    switch (dev->control_state.state) {
    case DATA_OUT:
        // accept DATA OUT packet
        if (read_data_out(dev) < 0)
            break;

        // more DATA OUT packets to arrive until request is complete
        if (dev->control_state.req.wLength - dev->control_state.ctrl_len <= dev->desc->bMaxPacketSize0)
            dev->control_state.state = LAST_DATA_OUT;
        break;

    case LAST_DATA_OUT:
        // accept last DATA OUT packet for request
        if (read_data_out(dev) < 0)
            break;

        // request is complete - process it
        if (dispatch_request(dev, &(dev->control_state.req))) {
            // submit STATUS IN packet
            qusb_dev_ep_write_packet(dev, 0, NULL, 0);
            dev->control_state.state = STATUS_IN;
        } else {
            stall(dev);
        }
        break;

    case STATUS_OUT:
        // accept STATUS OUT packet
        qusb_dev_ep_read_packet(dev, 0, NULL, 0);

        // control transfer is complete
        dev->control_state.state = IDLE;
        if (dev->control_state.completion)
            dev->control_state.completion(dev, &(dev->control_state.req));
        dev->control_state.completion = NULL;
        break;

    default:
        stall(dev);
    }
}

// Handle CONTROL IN events of endpoint 0
void _qusb_control_in(qusb_device* dev, __attribute__((unused)) uint8_t ep, __attribute__((unused)) uint32_t len)
{
    qusb_setup_data* req = &(dev->control_state.req);

    switch (dev->control_state.state) {
    case DATA_IN:
        // submit next DATA IN packet
        send_data_in(dev);
        break;

    case LAST_DATA_IN:
        dev->control_state.state = STATUS_OUT;
        break;

    case STATUS_IN:
        if (dev->control_state.completion)
            dev->control_state.completion(dev, &(dev->control_state.req));

        // set device address in case of SET_ADDRESS requests
        if (req->bmRequestType == 0 && req->bRequest == QUSB_REQ_SET_ADDRESS)
            _qusb_dev_set_address(dev, req->wValue);

        // control transfer is complete
        dev->control_state.state = IDLE;
        break;

    default:
        stall(dev);
    }
}
