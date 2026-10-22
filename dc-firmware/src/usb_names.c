// USB の製品名 (Chrome のポート選択、Windows のデバイス名、/dev/serial/by-id に出る)。Teensy コアの既定 "USB Serial" を上書きする
// (コアの usb_desc.c が弱シンボルで既定を持つ。製造者 "Teensyduino" とシリアル番号は既定のまま)。
// SD リーダー (dc-sd-reader) は "KART SD Reader"。console は PID (0x0483 / 0x04D5) で見分ける
#include <usb_names.h>

#define PRODUCT_NAME {'K', 'A', 'R', 'T', ' ', 'D', 'r', 'i', 'v', 'e', ' ', 'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r'}
#define PRODUCT_NAME_LEN 21

struct usb_string_descriptor_struct usb_string_product_name = {2 + PRODUCT_NAME_LEN * 2, 3, PRODUCT_NAME};
