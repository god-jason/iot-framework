#include "iot_error.h"

const char *iot_error_str(iot_err_t err)
{
    switch (err) {
    case IOT_OK:                return "OK";
    case IOT_ERR:               return "ERROR";
    case IOT_ERR_PARAM:         return "PARAM";
    case IOT_ERR_NOMEM:         return "NOMEM";
    case IOT_ERR_BUSY:          return "BUSY";
    case IOT_ERR_TIMEOUT:       return "TIMEOUT";
    case IOT_ERR_STATE:         return "STATE";
    case IOT_ERR_IO:            return "IO";
    case IOT_ERR_CRC:           return "CRC";
    case IOT_ERR_AUTH:          return "AUTH";
    case IOT_ERR_NOT_FOUND:     return "NOT_FOUND";
    case IOT_ERR_NOT_SUPPORTED: return "NOT_SUPPORTED";
    case IOT_ERR_OVERFLOW:      return "OVERFLOW";
    case IOT_ERR_INVALID:       return "INVALID";
    case IOT_ERR_AGAIN:         return "AGAIN";
    case IOT_ERR_EXISTS:        return "EXISTS";
    case IOT_ERR_CANCEL:        return "CANCEL";
    default:                    return "UNKNOWN";
    }
}