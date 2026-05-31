// cdcsend.c - send one framed CDC command packet to the Joypad CDC port and
// print the JSON response. Frame: [0xAA][len:2 LE][type:1][seq:1][payload][crc:2 LE]
// CRC-16-CCITT (poly 0x1021, init 0xFFFF) over type+seq+payload.
//
// Usage: cdcsend <serial-port> '<json payload>'
// Example: cdcsend /dev/cu.usbmodemXXX '{"cmd":"ROUTER.DPAD.SET","mode":0}'

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>

static uint16_t crc16(const uint8_t *d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <port> <json>\n", argv[0]); return 2; }
    const char *port = argv[1];
    const char *json = argv[2];
    size_t plen = strlen(json);

    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return 1; }

    struct termios t;
    tcgetattr(fd, &t);
    cfmakeraw(&t);
    cfsetspeed(&t, 115200);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);

    // Build frame
    uint8_t frame[8 + 1024];
    size_t i = 0;
    frame[i++] = 0xAA;
    frame[i++] = plen & 0xFF;
    frame[i++] = (plen >> 8) & 0xFF;
    uint8_t type = 0x01, seq = 0x01;
    frame[i++] = type;
    frame[i++] = seq;

    uint8_t crcbuf[2 + 1024];
    crcbuf[0] = type; crcbuf[1] = seq;
    memcpy(crcbuf + 2, json, plen);
    memcpy(frame + i, json, plen); i += plen;
    uint16_t crc = crc16(crcbuf, 2 + plen);
    frame[i++] = crc & 0xFF;
    frame[i++] = (crc >> 8) & 0xFF;

    if (write(fd, frame, i) != (ssize_t)i) { perror("write"); close(fd); return 1; }

    // Read response for ~800ms, scan for a CDC_MSG_RSP (type 0x02) frame.
    uint8_t buf[4096]; size_t got = 0;
    struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        double ms = (now.tv_sec - start.tv_sec) * 1000.0 + (now.tv_nsec - start.tv_nsec) / 1e6;
        if (ms > 800 || got >= sizeof(buf)) break;
        ssize_t n = read(fd, buf + got, sizeof(buf) - got);
        if (n > 0) got += n; else usleep(5000);
    }
    close(fd);

    // Parse frames: find 0xAA, len LE, type, seq, payload, crc.
    for (size_t p = 0; p + 5 <= got; ) {
        if (buf[p] != 0xAA) { p++; continue; }
        uint16_t len = buf[p+1] | (buf[p+2] << 8);
        uint8_t ftype = buf[p+3];
        if (p + 5 + len + 2 > got) break;
        if (ftype == 0x02) {  // RSP
            printf("response: %.*s\n", (int)len, &buf[p+5]);
            return 0;
        }
        p += 5 + len + 2;
    }
    fprintf(stderr, "no response frame seen (got %zu bytes)\n", got);
    return 1;
}
