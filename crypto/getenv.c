/*
 * Copyright 2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <stdlib.h>
#include "internal/cryptlib.h"

char *ossl_safe_getenv(const char *name)
{
    if (OPENSSL_issetugid())
        return NULL;
    return getenv(name);
}
