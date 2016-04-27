/* Copyright 2016 OpenMarket Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "olm/megolm.h"

#include <string.h>

#include "olm/crypto.h"

/* the seeds used in the HMAC-SHA-256 functions for each part of the ratchet.
 */
#define HASH_KEY_SEED_LENGTH 1
static uint8_t HASH_KEY_SEEDS[MEGOLM_RATCHET_PARTS][HASH_KEY_SEED_LENGTH] = {
    {0x00},
    {0x01},
    {0x02},
    {0x03}
};

static void rehash_part(
    uint8_t data[MEGOLM_RATCHET_PARTS][MEGOLM_RATCHET_PART_LENGTH],
    int rehash_from_part, int rehash_to_part,
    uint32_t old_counter, uint32_t new_counter
) {
    _olm_crypto_hmac_sha256(
        data[rehash_from_part],
        MEGOLM_RATCHET_PART_LENGTH,
        HASH_KEY_SEEDS[rehash_to_part], HASH_KEY_SEED_LENGTH,
        data[rehash_to_part]
    );
}



void megolm_init(Megolm *megolm, uint8_t const *random_data, uint32_t counter)
{
    megolm->counter = counter;
    memcpy(megolm->data, random_data, MEGOLM_RATCHET_LENGTH);
}


/* simplistic implementation for a single step */
void megolm_advance(Megolm *megolm) {
    uint32_t mask = 0x00FFFFFF;
    int h = 0;
    int i;

    megolm->counter++;

    /* figure out how much we need to rekey */
    while (h < (int)MEGOLM_RATCHET_PARTS) {
        if (!(megolm->counter & mask))
            break;
        h++;
        mask >>= 8;
    }

    /* now update R(h)...R(3) based on R(h) */
    for (i = MEGOLM_RATCHET_PARTS-1; i >= h; i--) {
        rehash_part(megolm->data, h, i, megolm->counter-1, megolm->counter);
    }
}

void megolm_advance_to(Megolm *megolm, uint32_t advance_to) {
    int j;

    /* starting with R0, see if we need to update each part of the hash */
    for (j = 0; j < (int)MEGOLM_RATCHET_PARTS; j++) {
        int shift = (MEGOLM_RATCHET_PARTS-j-1) * 8;
        uint32_t increment = 1 << shift;
        uint32_t next_counter;

        /* how many times to we need to rehash this part? */
        int steps = (advance_to >> shift) - (megolm->counter >> shift);
        if (steps == 0) {
            continue;
        }

        megolm->counter = megolm->counter & ~(increment - 1);
        next_counter = megolm->counter + increment;

        /* for all but the last step, we can just bump R(j) without regard
         * to R(j+1)...R(3).
         */
        while (steps > 1) {
            rehash_part(megolm->data, j, j, megolm->counter, next_counter);
            megolm->counter = next_counter;
            steps --;
            next_counter = megolm->counter + increment;
        }

        /* on the last step (except for j=3), we need to bump at least R(j+1);
         * depending on the target count, we may also need to bump R(j+2) and
         * R(j+3).
         */
        int k;
        switch(j) {
            case 0:
                if (!(advance_to & 0xFFFF00)) { k = 3; }
                else if (!(advance_to & 0xFF00)) { k = 2; }
                else { k = 1; }
                break;
            case 1:
                if (!(advance_to & 0xFF00)) { k = 3; }
                else { k = 2; }
                break;
            case 2:
            case 3:
                k = 3;
                break;
        }

        while (k >= j) {
            rehash_part(megolm->data, j, k, megolm->counter, next_counter);
            k--;
        }
        megolm->counter = next_counter;
    }
}
