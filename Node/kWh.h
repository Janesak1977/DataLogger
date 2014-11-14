#define NODEID 5            //5-10 - Energy monitoring nodes
#define networkGroup 0xD4

typedef struct {
    uint16_t Power;
    uint32_t Energy;
} payload_t;
