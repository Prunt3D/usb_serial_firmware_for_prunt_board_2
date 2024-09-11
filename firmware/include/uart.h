/*
 * USB Serial
 * 
 * Copyright (c) 2020 Manuel Bleichenbacher
 * Licensed under MIT License
 * https://opensource.org/licenses/MIT
 * 
 * UART interface
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>

#define UART_TX_BUF_LEN 1024
#define UART_RX_BUF_LEN 1024

enum class uart_stopbits
{
    _1_0 = 0,
    _1_5 = 1,
    _2_0 = 2,
};


enum class uart_parity
{
    none = 0,
    odd = 1,
    even = 2,
};


/**
 * @brief UART implementation
 */
class uart_impl
{
public:
    /// Initializes UART.
    void init();

    // Enable the UART
    void enable();

    /**
     * Polls for new UART events.
     */
    void poll();

    /**
     * @brief Submits the specified data for transmission.
     * 
     * The specified data is added to the transmission
     * buffer and transmitted asynchronously.
     * 
     * @param data pointer to byte array
     * @param len length of byte array
     */
    void transmit(const uint8_t *data, size_t len);

    /**
     * @brief Copies data from the receive buffer into the specified array.
     * 
     * The copied data is removed from the buffer.
     * 
     * @param data pointer to byte array
     * @param len length of the byte array
     * @return number of bytes copied to the byte array
     */
    size_t copy_rx_data(uint8_t *data, size_t len);

    /**
     * @brief Returns the length of received data in the receive buffer
     * 
     * @return length, in number of bytes
     */
    size_t rx_data_len();

    /**
     * Indicates of an RX buffer overrun has occurred.
     * 
     * This function will return `true` once for
     * each occurrence of an overrun.
     * 
     * @return `true` if overrun occurred.
     */
    bool has_rx_overrun_occurred();

    /**
     * @brief Returns the available space in the transmit buffer
     * 
     * @return space, in number of bytes
     */
    size_t tx_data_avail();

    /**
     * @brief Sets the line coding.
     * 
     * @param baudrate baud rate, in bps
     * @param databits data bits per byte
     * @param stopbits length of stop period, in bits
     * @param parity type of party bit
     */ 
    void set_coding(int baudrate, int databits, uart_stopbits stopbits, uart_parity parity);

    /**
     * @brief Gets the baud rate.
     * 
     * @return baud rate, in bps
     */
    int baudrate() { return _baudrate; }

    /**
     * @brief Gets the data bits per byte.
     * 
     * @return number of bits
     */
    int databits() { return _databits; }

    /**
     * @brief Gets the length of the stop period
     * 
     * @return length, in bits
     */
    uart_stopbits stopbits() { return _stopbits; }

    /**
     * @brief Gets the type of parity bit
     * 
     * @return parity type
     */
    uart_parity parity() { return _parity; }

private:
    /// Check if a chunk of data has been transmitted
    void poll_tx_complete();

    /// Try to transmit more data
    void start_transmission();

    /**
     * @brief Checks if RX buffer has been overrun.
     * 
     * If it has been overrun, data is discarded and the error state reset.
     * 
     * This functions must be called frequently in order to reliably detect an overrun
     * (more often than: RX buffer size * 10 bit/byte / maximum bit rate / 2)
     */
    void check_rx_overrun();

    /**
     * @brief Sets the baudrate
     * 
     * Uses the highest oversampling mode applicable to the baudrate.
     * Limits the baudrate if set too high.
     * 
     * @param baud baudrate (in bps)
     */
    void set_baudrate(int baud);

    /**
     * Deletes the high bit of each byte.
     * 
     * It is used to support 7 data bits.
     */
    static void clear_high_bits(uint8_t* buf, int buf_len);

    // Buffer for data to be transmitted via UART
    //  *  0 <= head < buf_len
    //  *  0 <= tail < buf_len
    //  *  head == tail => empty
    //  *  head + 1 == tail => full (modulo UART_TX_BUF_LEN)
    // `tx_buf_head` points to the positions where the next character
    // should be inserted. `tx_buf_tail` points to the character after
    // the last character that has been transmitted.
    uint8_t tx_buf[UART_TX_BUF_LEN];
    int tx_buf_head;
    int tx_buf_tail;

    // The number of bytes currently being transmitted
    int tx_size;

    // Buffer of data received via UART
    //  *  0 <= head < buf_len
    //  *  0 <= tail < buf_len
    //  *  head == tail => empty
    //  *  head + 1 == tail => full (modulo UART_RX_BUF_LEN)
    // `rx_buf_head` points to the positions where the next character
    // should be inserted. `rx_buf_tail` points to the character after
    // the last character that has been transmitted.
    uint8_t rx_buf[UART_RX_BUF_LEN];
    // int rx_buf_head: : managed by circular DMA controller
    int rx_buf_tail;

    // Last measured RX buffer size (to detect overrun)
    size_t last_rx_size;

    int _baudrate;
    int _databits;
    uart_stopbits _stopbits;
    uart_parity _parity;

    int rx_high_water_mark;
    int tx_max_chunk_size;

    bool is_transmitting;
    bool is_enabled;
    bool rx_overrun_occurred;
};

/// Global UART instance
extern uart_impl uart;
