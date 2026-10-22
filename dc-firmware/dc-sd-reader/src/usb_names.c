// USB の製品名 (エクスプローラー / Files の装置名、Chrome のポート選択に出る)。Teensy コアの既定 "Teensy MTP Disk" を上書きする。
// drive-controller (dc-firmware) は "KART Drive Controller"。console は PID (0x0483 / 0x04D5) で見分ける
#include <usb_names.h>

#define PRODUCT_NAME {'K', 'A', 'R', 'T', ' ', 'S', 'D', ' ', 'R', 'e', 'a', 'd', 'e', 'r'}
#define PRODUCT_NAME_LEN 14

struct usb_string_descriptor_struct usb_string_product_name = {2 + PRODUCT_NAME_LEN * 2, 3, PRODUCT_NAME};
