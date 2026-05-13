/* Copyright (C) NHR@FAU, University Erlangen-Nuremberg.
 * All rights reserved. This file is part of RabbitCT.
 * Use of this source code is governed by a MIT style
 * license that can be found in the LICENSE file. */
#include <stdio.h>
#include <string.h>

#include "algorithmRegistry.h"

/* ---- forward declarations for each compiled-in algorithm ---- */
extern int lolaOmpPrepare(RabbitCtGlobalData *);
extern int lolaOmpBackprojection(RabbitCtGlobalData *);
extern int lolaOmpFinish(RabbitCtGlobalData *);

extern int lolaBunnyPrepare(RabbitCtGlobalData *);
extern int lolaBunnyBackprojection(RabbitCtGlobalData *);
extern int lolaBunnyFinish(RabbitCtGlobalData *);

extern int lolaOptPrepare(RabbitCtGlobalData *);
extern int lolaOptBackprojection(RabbitCtGlobalData *);
extern int lolaOptFinish(RabbitCtGlobalData *);

extern int lolaAsmPrepare(RabbitCtGlobalData *);
extern int lolaAsmBackprojection(RabbitCtGlobalData *);
extern int lolaAsmFinish(RabbitCtGlobalData *);

#ifdef ENABLE_ISPC
extern int lolaIspcPrepare(RabbitCtGlobalData *);
extern int lolaIspcBackprojection(RabbitCtGlobalData *);
extern int lolaIspcFinish(RabbitCtGlobalData *);
#endif

#ifdef ENABLE_CUDA
extern int lolaCudaPrepare(RabbitCtGlobalData *);
extern int lolaCudaBackprojection(RabbitCtGlobalData *);
extern int lolaCudaFinish(RabbitCtGlobalData *);

extern int lolaCudaTexPrepare(RabbitCtGlobalData *);
extern int lolaCudaTexBackprojection(RabbitCtGlobalData *);
extern int lolaCudaTexFinish(RabbitCtGlobalData *);
#endif

/* ---- global function pointer variables ---- */
FncPrepareAlgorithmType FncPrepareAlgorithm;
FncAlgorithmIterationType FncAlgorithmIteration;
FncFinishAlgorithmType FncFinishAlgorithm;

/* ---- static registry: add new algorithms here ---- */
static const AlgorithmEntryType S_ALGORITHMS[] = {
  { "LolaOMP",   lolaOmpPrepare,   lolaOmpBackprojection,   lolaOmpFinish   },
  { "LolaBunny", lolaBunnyPrepare, lolaBunnyBackprojection, lolaBunnyFinish },
  { "LolaOPT",   lolaOptPrepare,   lolaOptBackprojection,   lolaOptFinish   },
  { "LolaASM",   lolaAsmPrepare,   lolaAsmBackprojection,   lolaAsmFinish   },
#ifdef ENABLE_ISPC
  { "LolaISPC",  lolaIspcPrepare,  lolaIspcBackprojection,  lolaIspcFinish  },
#endif
#ifdef ENABLE_CUDA
  { "LolaCUDA",  lolaCudaPrepare,  lolaCudaBackprojection,  lolaCudaFinish  },
  { "LolaCUDATex", lolaCudaTexPrepare, lolaCudaTexBackprojection, lolaCudaTexFinish  },
#endif
  { NULL,        NULL,             NULL,                    NULL            }  /* sentinel */
};

int algorithmRegistryFind(const char *name)
{
  for (int i = 0; S_ALGORITHMS[i].name != NULL; i++) {
    if (strcmp(S_ALGORITHMS[i].name, name) == 0) {
      FncPrepareAlgorithm   = S_ALGORITHMS[i].prepare;
      FncAlgorithmIteration = S_ALGORITHMS[i].iterate;
      FncFinishAlgorithm    = S_ALGORITHMS[i].finish;
      return 1;
    }
  }
  printf("Unknown algorithm: %s\n", name);
  algorithmRegistryList();
  return 0;
}

void algorithmRegistryList(void)
{
  printf("Available algorithms:\n");
  for (int i = 0; S_ALGORITHMS[i].name != NULL; i++) {
    printf("  %s\n", S_ALGORITHMS[i].name);
  }
}
