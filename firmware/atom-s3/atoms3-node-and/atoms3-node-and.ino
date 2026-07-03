// Compatibilite: point d'entree A&D historique.
//
// Le code executable est commun avec atoms3-node. Ce sketch ne fixe que les
// valeurs par defaut pour les installations deja habituees au firmware A&D.

#define FW_VARIANT_CODE "ATOM_AND"
#define FW_LOCK_SERIAL_DEFAULTS 1

#define DEFAULT_NODE_NAME    "ATOM-AND"
#define DEFAULT_BRAND        BRAND_AD
#define DEFAULT_BAUD         2400
#define DEFAULT_PARITY       1
#define DEFAULT_DATABITS     7
#define DEFAULT_STOPBITS     1
#define DEFAULT_POLL_CMD     "Q"
#define DEFAULT_LINE_TIMEOUT 300
#define DEFAULT_ZERO_CMD     "Z"

#include "../LabConnectAtomS3Node/LabConnectAtomS3NodeCore.h"
