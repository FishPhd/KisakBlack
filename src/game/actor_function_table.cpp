#include "actor.h"

#include "actor_dog_exposed.h"
#include "actor_generic.h"
#include "actor_exposed.h"
#include "actor_death.h"
#include "actor_pain.h"
#include "actor_negotiation.h"

const ai_funcs_t AIDogFuncTable[12] =
{
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  {
    Actor_Dog_Exposed_Start,
    Actor_Dog_Exposed_Finish,
    Actor_Dog_Exposed_Suspend,
    Actor_Generic_Resume,
    Actor_Dog_Exposed_Think,
    Actor_Exposed_Touch,
    Actor_Generic_Pain
  },
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  {
    Actor_BadPlace_Flee_Start,
    Actor_BadPlace_Flee_Finish,
    Actor_Generic_Suspend,
    Actor_Generic_Resume,
    Actor_BadPlace_Flee_Think,
    Actor_Generic_Touch,
    Actor_Generic_Pain
  },
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  {
    Actor_Death_Start,
    Actor_BadPlace_Flee_Finish,
    Actor_Generic_Suspend,
    Actor_Generic_Resume,
    Actor_Death_Think,
    Actor_Generic_Touch,
    Actor_Generic_Pain
  },
  {
    Actor_Pain_Start,
    Actor_Pain_Finish,
    Actor_Generic_Suspend,
    Actor_Generic_Resume,
    Actor_Pain_Think,
    Actor_Generic_Touch,
    Actor_Generic_Pain
  },
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  {
    Actor_Negotiation_Start,
    Actor_BadPlace_Flee_Finish,
    Actor_Generic_Suspend,
    Actor_Generic_Resume,
    Actor_Negotiation_Think,
    Actor_Generic_Touch,
    Actor_Generic_Pain
  }
};



const ai_funcs_t *AIFuncTable[1] = { AIDogFuncTable };

