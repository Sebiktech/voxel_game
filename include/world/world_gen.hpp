#pragma once
#include "chunk.hpp"

// Jednoduché vyplnenie: plochá vrstva s hrúbkou vo "fyzickej" miere.
// Napr. baseBlocks=4 znamená pôvodných 4 bloky -> teraz sa to automaticky násobí 2x.
void generateFlatChunk(Chunk& c, int baseBlocks = 4, uint16_t blockId = 1);

// Výšková mapa s lacným value-noise (žiadne externé lib).
// baseH a amp sú vo "pôvodných blokoch" (pred zmenšením), tu sa automaticky škálujú 2x.
void generateHeightmapChunk(Chunk& c,
    int baseH = 12,    // základná výška
    int amp = 6,     // amplitúda
    float freq = 0.07f,// frekvencia noise
    uint16_t topId = 1,// tráva
    uint16_t dirtId = 2 // hlina
);