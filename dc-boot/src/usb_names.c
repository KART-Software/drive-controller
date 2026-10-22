// どちらのイメージも無いときだけ USB (Serial) に出てくる。名前で分かるようにする
#include <usb_names.h>

#define PRODUCT_NAME {'K', 'A', 'R', 'T', ' ', 'B', 'o', 'o', 't'}
#define PRODUCT_NAME_LEN 9

struct usb_string_descriptor_struct usb_string_product_name = {2 + PRODUCT_NAME_LEN * 2, 3, PRODUCT_NAME};
