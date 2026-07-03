// Compatibilite: point d'entree Mettler historique.
//
// Le code executable est commun avec atoms3-node. Ce sketch ne fixe que les
// valeurs par defaut pour les installations deja habituees au firmware Mettler.

#define FW_VARIANT_CODE "ATOM_METTLER"
#define FW_LOCK_SERIAL_DEFAULTS 1

#define DEFAULT_NODE_NAME    "ATOM-METTLER"
#define DEFAULT_BRAND        BRAND_METTLER
#define DEFAULT_BAUD         9600
#define DEFAULT_PARITY       0
#define DEFAULT_DATABITS     8
#define DEFAULT_STOPBITS     1
#define DEFAULT_POLL_CMD     "SI"
#define DEFAULT_LINE_TIMEOUT 500
#define DEFAULT_ZERO_CMD     "Z"

#include "../LabConnectAtomS3Node/LabConnectAtomS3NodeCore.h"
