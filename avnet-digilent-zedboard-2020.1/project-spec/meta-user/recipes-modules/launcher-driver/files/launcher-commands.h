/* Rocket launcher specifics */
#define LAUNCHER_VENDOR_ID              0x2123
#define LAUNCHER_PRODUCT_ID             0x1010

#define LAUNCHER_NODE                   "launcher"
#define LAUNCHER_CTRL_BUFFER_SIZE       8
#define LAUNCHER_CTRL_REQUEST_TYPE      0x21
#define LAUNCHER_CTRL_REQUEST           0x09
#define LAUNCHER_CTRL_VALUE             0x0        
#define LAUNCHER_CTRL_INDEX             0x0
#define LAUNCHER_CTRL_COMMAND_PREFIX    0x02

#define LAUNCHER_STOP                   0x20
#define LAUNCHER_UP                     0x02
#define LAUNCHER_DOWN                   0x01
#define LAUNCHER_LEFT                   0x04
#define LAUNCHER_RIGHT                  0x08
#define LAUNCHER_UP_LEFT                (LAUNCHER_UP | LAUNCHER_LEFT)
#define LAUNCHER_DOWN_LEFT              (LAUNCHER_DOWN | LAUNCHER_LEFT)
#define LAUNCHER_UP_RIGHT               (LAUNCHER_UP | LAUNCHER_RIGHT)
#define LAUNCHER_DOWN_RIGHT             (LAUNCHER_DOWN | LAUNCHER_RIGHT)
#define LAUNCHER_FIRE                   0x10

#define LAUNCHER_MAX_UP                 0x80            /* 80 00 00 00 00 00 00 00 */
#define LAUNCHER_MAX_DOWN               0x40            /* 40 00 00 00 00 00 00 00 */
#define LAUNCHER_MAX_LEFT               0x04            /* 00 04 00 00 00 00 00 00 */
#define LAUNCHER_MAX_RIGHT              0x08            /* 00 08 00 00 00 00 00 00 */
