#define H263_RTP_MAX_HEADER_LEN     8       // H263 header max length for RFC2190 (mode A) or RFC2429
#define MAX_H263_PACKET             32      // max number of RTP packet per H263 Video Frame
#define H263_FRAME_SIZE            (PKT_PAYLOAD - H263_HEADER_MODE_A_SIZE)

struct RFC2190H263HeadersBasic
{
        //F=0             F=1
        //P=0   I/P frame       I/P mode b
        //P=1   B frame         B fame mode C
        uint32_t trb:9;
        uint32_t tr:3;
        uint32_t dbq:2;
        uint32_t r:3;
        uint32_t a:1;
        uint32_t s:1;
        uint32_t u:1;
        uint32_t i:1;
        uint32_t src:3;
        uint32_t ebits:3;
        uint32_t sbits:3;
        uint32_t p:1;
        uint32_t f:1;
};
#define H263P_HEADER_SIZE		2
#define H263_HEADER_MODE_A_SIZE 4
#define H263_HEADER_MODE_B_SIZE 8
#define H263_HEADER_MODE_C_SIZE 12

uint32_t rfc2190_append(uint8_t *dest, uint32_t destLen, uint8_t *buffer, uint32_t bufferLen);
void SendVideoFrameH263(struct ast_channel *chan, uint8_t *data, uint32_t size, int first, int last , int fps);
