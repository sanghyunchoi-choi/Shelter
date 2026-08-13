#include "protocol.h"

/**
 * @brief 패킷 데이터의 XOR(BCC)을 계산합니다.
 * @param data: 데이터 버퍼
 * @param len: XOR를 수행할 길이 (보통 ETX까지 포함)
 */
uint8_t Calculate_BCC(uint8_t *data, uint16_t len) {
    uint8_t bcc = 0;
    for (uint16_t i = 0; i < len; i++) {
        bcc ^= data[i];
    }
    return bcc;
}

/**
 * @brief 30바이트 보고용 패킷(BB)을 조립합니다.
 * @param buf: 저장할 버퍼 (최소 30바이트 이상)
 * @param id: 보드 ID
 * @param relays: 8개 릴레이 상태 배열 (0/1)
 * @param currents: 8개 채널 측정 전류 배열 (mA 단위)
 */
void Build_Report_Packet(uint8_t *buf, uint8_t id, uint8_t *relays, int16_t *currents) {
    buf[0] = PKT_STX;           // [0] STX
    buf[1] = id;                // [1] ID
    buf[2] = PKT_LEN_REPORT;    // [2] 전체 데이터 길이 (30)
    buf[3] = PKT_CMD_REPORT;    // [3] Command (0xBB)

    // 릴레이 1번(Index 4) ~ 8번(Index 25) 데이터 채우기
    // 각 릴레이당 3바이트씩 차지 (상태, 전류H, 전류L)
    for (int i = 0; i < 8; i++) {
        uint8_t start_idx = 4 + (i * 3);

        // 릴레이 상태: 앞자리는 릴레이 번호(1~8), 뒷자리는 상태(1/0)
        // 예: 1번 릴레이 ON -> 0x11, 1번 릴레이 OFF -> 0x10
        buf[start_idx] = ((i + 1) << 4) | (relays[i] & 0x01);

        // 측정 전류 (int16_t를 2바이트로 분할)
        buf[start_idx + 1] = (uint8_t)(currents[i] >> 8);   // 상위 바이트
        buf[start_idx + 2] = (uint8_t)(currents[i] & 0xFF); // 하위 바이트
    }

    buf[28] = PKT_ETX;          // [28] ETX

    // [29] BCC (0번부터 28번 ETX까지 XOR)
    buf[29] = Calculate_BCC(buf, 29);
}
