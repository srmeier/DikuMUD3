/*
 $Author: All $
 $RCSfile: justice.cpp,v $
 $Date: 2005/06/28 20:17:48 $
 $Revision: 2.7 $
 */

#include "justice.h"

#include "affect.h"
#include "comm.h"
#include "common.h"
#include "db.h"
#include "dilrun.h"
#include "handler.h"
#include "interpreter.h"
#include "money.h"
#include "slog.h"
#include "textutil.h"
#include "unit_affected_type.h"
#include "utils.h"
#include "vmelimits.h"

#include <cstring>
#include <ctime>
#include <string>

static int crime_serial_no = time(nullptr);

int new_crime_serial_no()
{
    int n = 0;

    n = time(nullptr);

    if (n > crime_serial_no)
    {
        crime_serial_no = n;
    }
    else
    {
        // If there's a race condition, just pick the next second
        crime_serial_no++;
    }

    return crime_serial_no;
}

void offend_legal_state(unit_data *ch, unit_data *victim)
{
    if (!IS_SET(CHAR_FLAGS(ch), CHAR_SELF_DEFENCE))
    {
        if (!CHAR_COMBAT(victim) && !IS_SET(CHAR_FLAGS(victim), CHAR_LEGAL_TARGET))
        {
            UCHAR(victim)->setCharacterFlag(CHAR_SELF_DEFENCE);
        }
    }

    /* Test for LEGAL_TARGET bit */
    if (IS_SET(CHAR_FLAGS(victim), CHAR_PROTECTED) && !IS_SET(CHAR_FLAGS(victim), CHAR_LEGAL_TARGET) &&
        !IS_SET(CHAR_FLAGS(ch), CHAR_SELF_DEFENCE))
    {
        UCHAR(ch)->setCharacterFlag(CHAR_LEGAL_TARGET);
    }
}

// MS2020: Have the NPC walk to the designated room
/*
void npc_walkto(class unit_data *u, class unit_data *toroom)
{
   struct diltemplate *tmpl;
   char buf[500];

   // Check if we are already in rescue mode, then ignore.
   if (dil_find("to_the_rescue@midgaard", u))
      return;

   assert(IS_ROOM(toroom));

   buf[0] = 0;
   strcpy(buf, FI_NAME(UNIT_FILE_INDEX(toroom)));
   strcat(buf, "@");
   strcat(buf, FI_ZONENAME(UNIT_FILE_INDEX(toroom)));

   tmpl = find_dil_template("to_the_rescue@midgaard");
   if (!tmpl)
   {
      slog(LOG_ALL, 0, "npc_walkto: Unable to locate DIL to_the_rescue@midgaard");
      return;
   }
   class dilprg *prg = dil_copy_template(tmpl, u, NULL);
   if (prg)
   {
      prg->waitcmd = WAITCMD_MAXINST - 1;
      prg->fp->vars[0].val.string = str_dup(buf);
      //prg->fp->vars[0].val.unitptr  = toroom; why didn't this work?
      dil_activate(prg);
   }
}
*/

// Activate the add_crime@justice DIL
//
//
void add_crime(unit_data *criminal, unit_data *victim, int type)
{
    diltemplate *tmpl = nullptr;
    dilprg *prg = nullptr;
    int crime_no = 0;

    if (str_is_empty(criminal->getNames().Name()))
    {
        slog(LOG_ALL, 0, "JUSTICE: NULL name in criminal");
        return;
    }

    if (str_is_empty(victim->getNames().Name()))
    {
        slog(LOG_ALL, 0, "JUSTICE: NULL name in victim");
        return;
    }

    crime_no = new_crime_serial_no();

    tmpl = find_dil_template("add_crime@justice");

    if (tmpl)
    {
        prg = dil_copy_template(tmpl, criminal, nullptr);

        if (prg)
        {
            prg->waitcmd = WAITCMD_MAXINST - 1;

            prg->fp->vars[0].val.unitptr = criminal;
            prg->fp->vars[1].val.unitptr = victim;
            prg->fp->vars[2].val.integer = type;
            prg->fp->vars[3].val.integer = crime_no;
            prg->fp->vars[4].val.integer = CRIME_LIFE + 2;
            dil_add_secure(prg, criminal, prg->fp->tmpl->core);
            dil_add_secure(prg, victim, prg->fp->tmpl->core);
            dil_activate(prg);
        }
    }
}

// criminal points to the perpetrating char. Mandatory.
// victim is the char subjected to a crime. Mandatory.
// crime_type can be CRIME_EXTRA, CRIME_STEALING, CRIME_MURDER, CRIME_PK
// active ?
//
void log_crime(unit_data *criminal, unit_data *victim, ubit8 crime_type, int active)
{
    int i = 0;
    int j = 0;
    diltemplate *tmpl = nullptr;
    dilprg *prg = nullptr;
    dilprg *prg2 = nullptr;
    dilprg *prg3 = nullptr;

    if (criminal == nullptr)
    {
        slog(LOG_ALL, 0, "log_crime() NULL criminal");
        return;
    }

    if (victim == nullptr)
    {
        slog(LOG_ALL, 0, "log_crime() NULL victim");
        return;
    }

    if (!criminal->isChar() || !victim->isChar())
    {
        slog(LOG_ALL, 0, "log_crime() criminal or victim not IS_CHAR");
        return;
    }

    /* When victim is legal target you can't get accused from it. */
    if (IS_SET(CHAR_FLAGS(victim), CHAR_LEGAL_TARGET) && ((crime_type == CRIME_MURDER) || (crime_type == CRIME_PK)))
    {
        return;
    }

    // It's OK to kill NPCs that are not "protected"
    if (victim->isNPC() && !IS_SET(CHAR_FLAGS(victim), CHAR_PROTECTED))
    {
        return;
    }

    // First let's deal with registering the crime the criminal committed
    // add_crime(criminal, victim, crime_type);

    // prepare the set_witness function
    tmpl = find_dil_template("set_witness@justice");
    if (tmpl)
    {
        prg = dil_copy_template(tmpl, victim, nullptr);

        if (prg)
        {
            prg->waitcmd = WAITCMD_MAXINST - 1;
            prg->fp->vars[0].val.unitptr = criminal;
            prg->fp->vars[1].val.unitptr = victim;
            prg->fp->vars[2].val.integer = crime_serial_no;
            prg->fp->vars[3].val.integer = crime_type;
            prg->fp->vars[4].val.integer = active;
            prg->fp->vars[5].val.string = str_dup(victim->getNames().Name());
            dil_add_secure(prg, criminal, prg->fp->tmpl->core);
            dil_add_secure(prg, victim, prg->fp->tmpl->core);
            dil_activate(prg);
        }
    }

    // Find any bystanders and register them as witnesses
    scan4_unit(victim, UNIT_ST_PC | UNIT_ST_NPC);

    for (i = 0; i < g_unit_vector.top; i++)
    {
        if (CHAR_CAN_SEE(UVI(i), criminal))
        {
            /* set_witness(criminal, UVI(i), crime_serial_no, crime_type, active); */
            tmpl = find_dil_template("set_witness@justice");
            if (tmpl)
            {
                prg2 = dil_copy_template(tmpl, UVI(i), nullptr);

                if (prg2)
                {
                    prg2->waitcmd = WAITCMD_MAXINST - 1;
                    prg2->fp->vars[0].val.unitptr = criminal;
                    prg2->fp->vars[1].val.unitptr = UVI(i);
                    prg2->fp->vars[2].val.integer = crime_serial_no;
                    prg2->fp->vars[3].val.integer = crime_type;
                    prg2->fp->vars[4].val.integer = active;
                    prg2->fp->vars[5].val.string = str_dup(victim->getNames().Name());
                    dil_add_secure(prg2, criminal, prg2->fp->tmpl->core);
                    dil_add_secure(prg2, UVI(i), prg2->fp->tmpl->core);
                    dil_activate(prg2);
                }
            }
        }
    }

    // Find any CHAR fighting the victim - they are complicit to the crime
    for (j = 0; j < g_unit_vector.top; j++)
    {
        if (CHAR_COMBAT(UVI(j)) && CHAR_COMBAT(UVI(j))->FindOpponent(victim) && UVI(j) != criminal)
        {
            // add_crime(UVI(j), victim, crime_type);

            for (i = 0; i < g_unit_vector.top; i++)
            {
                if (CHAR_CAN_SEE(UVI(i), UVI(j)))
                {
                    tmpl = find_dil_template("set_witness@justice");
                    prg3 = dil_copy_template(tmpl, UVI(i), nullptr);

                    if (prg3)
                    {
                        prg3->waitcmd = WAITCMD_MAXINST - 1;
                        prg3->fp->vars[0].val.unitptr = criminal;
                        prg3->fp->vars[1].val.unitptr = UVI(j);
                        prg3->fp->vars[2].val.integer = crime_serial_no;
                        prg3->fp->vars[3].val.integer = crime_type;
                        prg3->fp->vars[4].val.integer = active;
                        prg3->fp->vars[5].val.string = str_dup(victim->getNames().Name());
                        dil_add_secure(prg3, criminal, prg3->fp->tmpl->core);
                        dil_add_secure(prg3, UVI(j), prg3->fp->tmpl->core);
                        dil_activate(prg3);
                    }
                }
            }
            /*  set_witness(UVI(j), UVI(i), crime_serial_no, crime_type, active); */
        }
    }
}

/* ---------------------------------------------------------------------- */
/*                    M O V E M E N T   F U N C T I O N S                 */
/* ---------------------------------------------------------------------- */

/* SFUN_NPC_VISIT_ROOM. Used by npc_set_visit() to move back and    */
/* forth.                                                           */
/*
int npc_visit_room (struct spec_arg *sarg)
{
    struct visit_data *vd;
    int i;

    vd = (struct visit_data *) sarg->fptr->data;

    if (sarg->cmd->no == CMD_AUTO_EXTRACT)
    {
        if (vd->data)
        {
            (*vd->what_now) (0, vd);	// Give it a chance to free data

            FREE (vd);
            sarg->fptr->data = 0;
        }
        return SFR_BLOCK;
    }

    if (sarg->cmd->no != CMD_AUTO_TICK)
        return vd->non_tick_return;

    i = MOVE_FAILED;

    // FUCK FUCK FUCK
    //npc_move (sarg->owner, vd->go_to);

    if (i == MOVE_DEAD)		// Npc died
        return SFR_BLOCK;

    if (i == MOVE_FAILED)
    {
        destroy_fptr (sarg->owner, sarg->fptr);
        return SFR_BLOCK;
    }

    if ((i == MOVE_GOAL) && CHAR_IS_READY (sarg->owner))	// Reached dest?
    {
        // We may not be finished walking - set to 0

        if ((i = (*vd->what_now) (sarg->owner, vd)) == DESTROY_ME)
        {
            destroy_fptr (sarg->owner, sarg->fptr);
            return SFR_BLOCK;
        }

        // vd->go_to = 0;

        return i;
    }

    return SFR_SHARE;		// No other tick functions allowed (PRIORITY SET)
}
*/

/* Initiate a NPC to go to 'dest_room', call the 'what_now' and then */
/* return to its original room                                      */
/* The *data may be any datapointer which what_now can use          */
/*                                                                  */
/*
void npc_set_visit (class unit_data * npc, class unit_data * dest_room,
                    int what_now (const class unit_data *, struct visit_data *),
                    void *data, int non_tick_return)
{
    npc_walkto(npc, unit_room (dest_room));

    // Have not solved the "what now" nor return to original :)

    CREATE (vd, struct visit_data, 1);

    vd->non_tick_return = non_tick_return;
    vd->state = 0;
    vd->start_room = u;
    vd->dest_room = dest_room;
    vd->go_to = dest_room;
    vd->what_now = what_now;
    vd->data = data;

    fp2 = create_fptr (npc, SFUN_NPC_VISIT_ROOM, WAIT_SEC * 5,
                            SFB_PRIORITY | SFB_RANTIME | SFB_TICK, vd);

    / Now move this created fptr down below SFUN_PROTECT_LAWFUL /
    fp1 = find_fptr (npc, SFUN_PROTECT_LAWFUL);

    if (fp1)
    {
        fp2 = UNIT_FUNC (npc);
        assert (fp2->next);
        UNIT_FUNC (npc) = UNIT_FUNC (npc)->next;
        fp2->next = fp1->next;
        fp1->next = fp2;
    }
}*/

/* ---------------------------------------------------------------------- */
/*                    A C C U S E   F U N C T I O N S                     */
/* ---------------------------------------------------------------------- */

/* The witness list takes care of itself */
/* void remove_crime(struct char_crime_data *crime)
{
    struct char_crime_data *c;

    if (crime == crime_list)
        crime_list = crime->next;
    else
    {
        for (c = crime_list; c->next; c = c->next)
            if (c->next == crime)
            {
                c->next = c->next->next;
                break;
            }
    }

    FREE(crime);
}
*/
/* void set_reward_char(class unit_data *ch, int crimes)
{
    class unit_affected_type *paf;
    class unit_affected_type af;
    int xp = 0, gold = 0;

    int lose_exp(class unit_data * ch);

     Just to make sure in case anyone gets randomly rewarded
    REMOVE_BIT(CHAR_FLAGS(ch), CHAR_PROTECTED);
    SET_BIT(CHAR_FLAGS(ch), CHAR_OUTLAW);

    if ((paf = affected_by_spell(ch, ID_REWARD)))
    {
        paf->data[0] = MAX(xp, paf->data[0]);
        paf->data[1] = MAX(gold, paf->data[1]);
        paf->data[2]++;
        return;
    }

    gold = money_round_up(CHAR_LEVEL(ch) * CHAR_LEVEL(ch) * 100,
                          DEF_CURRENCY, 1);
    xp = CHAR_LEVEL(ch) * 20 + 50;

    if (IS_PC(ch))
    {
        PC_CRIMES(ch) += crimes;
        xp = MIN(lose_exp(ch) / 2, xp);
    }

    af.id = ID_REWARD;
    af.duration = 1;
    af.beat = 0;
    af.firstf_i = TIF_REWARD_ON;
    af.tickf_i = TIF_NONE;
    af.lastf_i = TIF_REWARD_OFF;
    af.applyf_i = APF_NONE;

    af.data[0] = xp;
    af.data[1] = gold;
    af.data[2] = 1;

    create_affect(ch, &af);
}

void set_witness(class unit_data *criminal, class unit_data *witness,
                 int no, int type, int show = TRUE)
{
    class unit_affected_type af;

    void activate_accuse(class unit_data * npc, ubit8 crime_type,
                         const char *cname);
    struct diltemplate *tmpl;

    if (!IS_PC(criminal))
        return;

    // Dont check for AWAKE here since the dead victim is to be a witness!

    if (witness == criminal)
        return;

    if (IS_NPC(witness) && !IS_SET(CHAR_FLAGS(witness), CHAR_PROTECTED))
        return;

    if (show)
        act("You just witnessed a crime committed by $1n.", A_ALWAYS, criminal, cActParameter(), witness, TO_VICT);

     Create witness data
    af.id = ID_WITNESS;
    af.beat = WAIT_SEC * 5 * 60;  Every 5 minutes
    af.duration = CRIME_LIFE;
    af.data[0] = no;
    af.data[1] = type;
    af.data[2] = PC_ID(criminal);

    af.firstf_i = TIF_NONE;
    af.tickf_i = TIF_NONE;
    af.lastf_i = TIF_NONE;
    af.applyf_i = APF_NONE;

    create_affect(witness, &af);

    if (IS_NPC(witness) && show)
    {   activate_accuse(witness, type, UNIT_NAME(criminal)); */
/*    tmpl = find_dil_template("activate_accuse@justice");
        class dilprg *prg;

        prg = dil_copy_template(tmpl, witness, NULL);

        if (prg)
        {
            prg->waitcmd = WAITCMD_MAXINST - 1;

            prg->fp->vars[1].val.integer = type;
            prg->fp->vars[2].val.string = str_dup(UNIT_NAME(criminal));

            dil_activate(prg);
        }
      }
}
*/

/* add new crime crime_list old code: */
/*   CREATE(crime, struct char_crime_data, 1);
    crime->next = crime_list;
    crime_list = crime;

    crime->crime_nr = crime_no;
    crime->id = PC_ID(criminal);
    strcpy(crime->name_criminal, UNIT_NAME(criminal));
    strncpy(crime->victim, UNIT_NAME(victim), 30);
    crime->crime_type = type;
    crime->ticks_to_neutralize = CRIME_LIFE + 2;
    crime->reported = FALSE;
*/

/* char *crime_victim_name(int crime_no, int id)
{
    struct char_crime_data *c;

    for (c = crime_list; c; c = c->next)
        if ((c->crime_nr == (ubit32)crime_no) && (c->id == id))
            return c->name_criminal;

    return NULL;
}
*/

/* Got to have this loaded somewhere */

/* void save_accusation(struct char_crime_data *crime,
                     const class unit_data *accuser)
{
    FILE *file;

    if (!(file =
              fopen_cache(str_cc(g_cServerConfig.m_libdir, CRIME_ACCUSE_FILE),
                          "a+")))
    {
        slog(LOG_OFF, 0, "register_crime: can't open file.");
        assert(FALSE);
    }

    time_t t = time(0);

    fprintf(file, "%5u  %4d  %4d %1d [%s]  [%s]   %12lu %s",
            crime->crime_nr, crime->id, crime->crime_type,
            crime->reported,
            UNIT_NAME((class unit_data *)accuser), crime->victim,
            t, ctime(&t));
    fflush(file);
     Was fclose(file) */
//}

/*
static void
crime_counter(class unit_data *criminal, int incr, int first_accuse)
{
    if ((PC_CRIMES(criminal) + incr) / CRIME_NONPRO >
        PC_CRIMES(criminal) / CRIME_NONPRO)
    {
        if (!IS_SET(CHAR_FLAGS(criminal), CHAR_OUTLAW))
            REMOVE_BIT(CHAR_FLAGS(criminal), CHAR_PROTECTED);
    }

    if ((PC_CRIMES(criminal) + incr) / CRIME_OUTLAW >
        PC_CRIMES(criminal) / CRIME_OUTLAW)
        SET_BIT(CHAR_FLAGS(criminal), CHAR_OUTLAW | CHAR_PROTECTED);

     I see no reason for first_accuse????
       if (first_accuse &&
       ((PC_CRIMES(criminal)+incr) / CRIME_REWARD >
       PC_CRIMES(criminal) / CRIME_REWARD)) */

/*  if (((PC_CRIMES(criminal) + incr) / CRIME_REWARD >
         PC_CRIMES(criminal) / CRIME_REWARD))
    {
        // PC_CRIMES(criminal) += incr done in set_reward!
        set_reward_char(criminal, incr);
    }
    else
    {
        PC_CRIMES(criminal) += incr;
    }
}

static void
update_criminal(const class unit_data *deputy,
                const char *pPlyName, int pidx,
                struct char_crime_data *crime, int first_accuse)
{
    class unit_data *criminal = NULL;
    int loaded = FALSE;
    int incr;

    void save_player_file(class unit_data * pc);

     Modified find_descriptor */
/*  for (criminal = g_unit_list; criminal; criminal = criminal->gnext)
        if (IS_PC(criminal) && PC_ID(criminal) == pidx)
        {
            act("$1n tells you, 'You are in trouble, you good-for-nothing ...'",
                A_SOMEONE, deputy, cActParameter(), criminal, TO_VICT);
            break;
        }

     These should be protectedmob<lawenforcer<pc * crimeseriousness */
/* if (first_accuse)
        incr = crime->crime_type;
    else
        incr = CRIME_EXTRA;

    if (!criminal)
    {
        criminal = load_player(pPlyName);
        if (criminal == NULL)
            return;

        loaded = TRUE;
    }

    if (criminal)
        crime_counter(criminal, incr, first_accuse);

    if (loaded)
    {
        class descriptor_data *d = find_descriptor(pPlyName, NULL);

        if (d && d->character)
            crime_counter(d->character, incr, first_accuse);

        save_player_file(criminal);
        extract_unit(criminal);
    }
}

int accuse(struct spec_arg *sarg)
{
    class unit_affected_type *af;
    struct char_crime_data *crime;

    int crime_type = 0;       {CRIME_MURDER,CRIME_STEALING} */
/*  char arg1[80], arg2[80];  will hold accused and crime    */
/*   int pid;

    int find_player_id(char *);

     legal command ? */
/*    if (!is_command(sarg->cmd, "accuse"))
        return SFR_SHARE;

    strcpy(arg1, UNIT_NAME(sarg->owner));

    if (IS_NPC(sarg->activator) && CHAR_POS(sarg->owner) == POSITION_SLEEPING)
        command_interpreter(sarg->activator, "wake");

    if (CHAR_POS(sarg->owner) < POSITION_SLEEPING ||
        CHAR_POS(sarg->owner) == POSITION_FIGHTING)
    {
        act("$1n seems busy right now.", A_SOMEONE, sarg->owner,
            sarg->activator, cActParameter(), TO_ROOM);
        return SFR_BLOCK;
    }

     legal args ? */
/*  argument_interpreter(sarg->arg, arg1, arg2);  extract args from arg :) */

/*    if (str_is_empty(arg1))
    {
        act("$1n says, 'Yes... who?'", A_SOMEONE, sarg->owner, sarg->activator,
            cActParameter(), TO_ROOM);
        return SFR_BLOCK;
    }
    else
    {
        if (str_is_empty(arg2))
        {
            act("$1n says, 'What do you wish to accuse $3t of?'",
                A_SOMEONE, sarg->owner, sarg->activator, arg1, TO_ROOM);
            return SFR_BLOCK;
        }
    }

    ok, what does it want ? */
/*    if (!(strcmp(arg2, "murder")))
    {
        crime_type = CRIME_MURDER;
        act("$1n says, 'Murder... lets see', and looks through his files.",
            A_SOMEONE, sarg->owner, sarg->activator, cActParameter(), TO_ROOM);
    }
    else if (!(strcmp(arg2, "stealing")))
    {
        crime_type = CRIME_STEALING;
        act("$1n says, 'Stealing... lets see', and looks through his files.",
            A_SOMEONE, sarg->owner, sarg->activator, cActParameter(), TO_ROOM);
    }
    else
    {
        act("$1n says, 'Are you accusing of murder or stealing?'",
            A_SOMEONE, sarg->owner, sarg->activator, cActParameter(), TO_ROOM);
        return SFR_BLOCK;
    }

    if ((pid = find_player_id(arg1)) == -1)
    {
        act("$1n says, 'I have never heard of this so called $2t.'",
            A_SOMEONE, sarg->owner, arg1, sarg->activator, TO_ROOM);
        return SFR_BLOCK;
    }

    act("$1n accuses $3t of $2t.", A_SOMEONE, sarg->activator, arg2, arg1,
        TO_ROOM);
    act("$1n says, 'Ah yes... $2t'", A_SOMEONE, sarg->owner, arg2,
        sarg->activator, TO_ROOM);

    for (crime = crime_list; crime; crime = crime->next)
    {
        if (pid == crime->id)
        {
            if ((crime_type == CRIME_MURDER) &&
                (crime->crime_type != CRIME_MURDER) &&
                (crime->crime_type != CRIME_PK))
                continue;

            if ((crime_type == CRIME_STEALING) &&
                (crime->crime_type != CRIME_STEALING))
                continue;

             Check the witness */
/*          for (af = UNIT_AFFECTED(sarg->activator); af; af = af->next)
                if ((af->id == ID_WITNESS) && (af->data[0] ==
                                               (int)crime->crime_nr))
                {
                    act("$1n says, 'Thank you very much $3N, I will stop $2t.'",
                        A_SOMEONE, sarg->owner, arg1, sarg->activator, TO_ROOM);

                    if (!(crime->reported))
                        update_criminal(sarg->owner, arg1, pid, crime, TRUE);
                    else
                        update_criminal(sarg->owner, arg1, pid, crime, FALSE);

                     Mark crime as already reported */
/*                  crime->reported = TRUE;
                    save_accusation(crime, sarg->activator);

                    if (UNIT_ALIGNMENT(sarg->activator) > -1000)
                        UNIT_ALIGNMENT(sarg->activator) += 100;

                    destroy_affect(af);

                    return SFR_BLOCK;
                }
        }
    }

    act("$1n says, 'Sorry $3n, but I don't find your evidence convincing.'",
        A_SOMEONE, sarg->owner, cActParameter(), sarg->activator, TO_ROOM);

    return SFR_BLOCK;
}
*/
/* void update_crimes(void)
{
    struct char_crime_data *c, *next_dude;

    for (c = crime_list; c; c = next_dude)
    {
        next_dude = c->next;
        if ((--(c->ticks_to_neutralize)) <= 0)
            remove_crime(c);
    }
}
*/
/* ---------------------------------------------------------------------- */
/*                      A C C U S E   F U N C T I O N S                   */
/* ---------------------------------------------------------------------- */

/*struct npc_accuse_data
{
    char *criminal_name;
    ubit8 crime_type;
    int was_wimpy;
};
*/
/* For use with the walk.c system. When at captain accuse the criminal */
/* and then return to previous duties                                  */
/*                                                                     */
/*int npc_accuse(const class unit_data *npc, struct visit_data *vd)
{
    char str[80];
    class unit_affected_type *af;
    struct npc_accuse_data *nad;

    nad = (struct npc_accuse_data *)vd->data;

    if (!npc)
    {
        FREE(nad->criminal_name);
        FREE(nad);
        vd->data = 0;
        return DESTROY_ME;
    }

    switch (vd->state)
    {
    case 0:
    {

        af = affected_by_spell(npc, ID_WITNESS);
        if (af == NULL)
        {
            vd->state++;
            return SFR_BLOCK;
        }

        strcpy(str, "accuse ");
        char *cvn = NULL;
        cvn = crime_victim_name(af->data[0], af->data[2]);
        if (cvn == NULL)
        {
            strcat(str, cvn);
        }
        else
        {
            strcat(str, "");
        }

        if ((af->data[1] == CRIME_MURDER) || (af->data[1] == CRIME_PK))
            strcat(str, " murder");
        else
            strcat(str, " stealing");
        command_interpreter((class unit_data *)npc, str);
        return SFR_BLOCK;
    }
    break;
    case 1:
        if (!nad->was_wimpy)
            REMOVE_BIT(CHAR_FLAGS(npc), CHAR_WIMPY);
        vd->go_to = vd->start_room;
        vd->state++;
        return SFR_BLOCK;
    }

    return DESTROY_ME;
}
i*/
// MS2020: this must be broken now. Convert to DIL?
// id activate_accuse(class unit_data *npc, ubit8 crime_type, const char *cname)
//
//  struct npc_accuse_data *nad;
//  class unit_data *prison;
//  class unit_fptr *fptr;
//  struct visit_data *vd;

// GEN: get accuse room in here */
// How to find the nearest accuse room? */
// Name of the accusation location must be the same everywhere!  */

/* If we are already on the way to accuse, it will take care
       of everything */
/*   if ((fptr = find_fptr(npc, SFUN_NPC_VISIT_ROOM)) &&
        affected_by_spell(npc, ID_WITNESS))
    {
        vd = (struct visit_data *)fptr->data;
        if (vd && (vd->what_now == npc_accuse) && (vd->state == 0))
            return;  Do nothing */
//}

/* We don't want captain to accuse, he can't activate himself! */
/* if (find_fptr(npc, SFUN_ACCUSE))
        return;

    if ((prison = world_room(UNIT_IN(npc)->fi->zone->name, ACCUSELOC_NAME)) ||
        (prison = world_room(npc->fi->zone->name, ACCUSELOC_NAME)))
    {
        CREATE(nad, struct npc_accuse_data, 1);
        nad->criminal_name = str_dup(cname);
        nad->crime_type = crime_type;
        nad->was_wimpy = IS_SET(CHAR_FLAGS(npc), CHAR_WIMPY);

        SET_BIT(CHAR_FLAGS(npc), CHAR_WIMPY);

        //npc_set_visit (npc, prison, npc_accuse, nad, SFR_SHARE);
        npc_walkto(npc, unit_room(prison));
    }
    else
        act("$1n complains about the lack of law and order in this place.",
            A_HIDEINV, npc, cActParameter(), cActParameter(), TO_ROOM);
}
*/
/* ---------------------------------------------------------------------- */
/*                      A R R E S T   F U N C T I O N S                   */
/* ---------------------------------------------------------------------- */

/* static int crime_in_progress(class unit_data *att, class unit_data *def)
{
    if (att && def && IS_CHAR(att) && IS_CHAR(def))
    {
         If the attacker is attacking someone protected, or if the
           attacker is protected and is attacking someone non-protected
           then go in action */

/*       if (!IS_SET(CHAR_FLAGS(att), CHAR_SELF_DEFENCE) &&
            (((IS_SET(CHAR_FLAGS(def), CHAR_PROTECTED) &&
               !IS_SET(CHAR_FLAGS(def), CHAR_LEGAL_TARGET)) ||
              (!IS_SET(CHAR_FLAGS(att), CHAR_PROTECTED) &&
               IS_SET(CHAR_FLAGS(def), CHAR_PROTECTED)))))
            return TRUE;
    }
    return FALSE;
}

 Help another friendly guard! :-) */
/*
int guard_assist(const class unit_data *npc, struct visit_data *vd)
{
   switch (vd->state++)
   {
   case 0:
      command_interpreter((class unit_data *)npc, "peer");
      return SFR_BLOCK;

   case 1: // Just wait a little while...
      return SFR_BLOCK;

   case 2:
      if (CHAR_COMBAT(npc))
      {
         vd->state = 2;
         return SFR_BLOCK;
      }
      vd->go_to = vd->start_room;
      return SFR_BLOCK;
   }

   // We are done, kill me!
   return DESTROY_ME;
}*/

/* 'Guard' needs help. Call his friends... :-)   */
/*                                               */
/*
void call_guards(class unit_data *guard)
{
   class zone_type *zone;
   class unit_data *u;

   if (!IS_ROOM(UNIT_IN(guard)))
      return;

   zone = unit_zone(guard);

   for (u = g_unit_list; u; u = u->gnext)
   {
      membug_verify_class(u);
      assert(!u->is_destructed());

      if (IS_NPC(u) && IS_ROOM(UNIT_IN(u)) &&
          zone == UNIT_FILE_INDEX(UNIT_IN(u))->zone && u != guard)
      {
         // If this NPC has a protect lawful DIL then they may answer the whistle call
         if (!dil_find("protect_lawful@justice", u))
            continue;

         // If the NPC is on its way to report a crime, don't answer the call.
         if (!number(0, 5) && !dil_find("activate_accuse@justice", u))
            npc_walkto(u, UNIT_IN(guard));
      }
   }
}
*/

/* This routine protects lawful characters                               */
/* SFUN_PROTECT_LAWFUL                                                   */
/* int protect_lawful(struct spec_arg *sarg)
{
    int i;

    if ((sarg->cmd->no < CMD_AUTO_DEATH) || sarg->activator == sarg->owner)
        return SFR_SHARE;

    if (!IS_CHAR(sarg->owner) || !CHAR_AWAKE(sarg->owner) || CHAR_COMBAT(sarg->owner))
        return SFR_SHARE;

    if (sarg->cmd->no == CMD_AUTO_TICK)
    {
        scan4_unit(sarg->owner, UNIT_ST_NPC); ///Implicit can see */

/*      for (i = 0; i < g_unit_vector.top; i++)
        {
            if (CHAR_CAN_SEE(sarg->owner, UVI(i)) &&
                UNIT_IS_EVIL(UVI(i)))
            {
                SET_BIT(CHAR_FLAGS(UVI(i)), CHAR_LEGAL_TARGET);
                act("$1n blows in a small whistle!  'UUIIIIIIIHHHHH'",
                    A_SOMEONE, sarg->owner, cActParameter(), sarg->activator, TO_ROOM);
                call_guards(sarg->owner);
                // MS2020 why dont sleeping guards in the room wake up? */
/*               simple_one_hit(sarg->owner, UVI(i));
                REMOVE_BIT(CHAR_FLAGS(UVI(i)), CHAR_SELF_DEFENCE);
                return SFR_BLOCK;
            }
        }
    }
    else
    {
        if (!IS_CHAR(sarg->activator))
            return SFR_SHARE;

        if (sarg->cmd->no == CMD_AUTO_DEATH)
        {
            if (crime_in_progress(CHAR_FIGHTING(sarg->activator), sarg->activator))
            {
                simple_one_hit(sarg->owner, CHAR_FIGHTING(sarg->activator));
                REMOVE_BIT(CHAR_FLAGS(sarg->activator), CHAR_SELF_DEFENCE);
                return SFR_BLOCK;
            }
        }
        else // COMBAT? */
//    {
/*          if (crime_in_progress(sarg->activator, CHAR_FIGHTING(sarg->activator)))
            {
                simple_one_hit(sarg->owner, sarg->activator);
                REMOVE_BIT(CHAR_FLAGS(sarg->activator), CHAR_SELF_DEFENCE);
                return SFR_BLOCK;
            }
        }
    }

    return SFR_SHARE;
}

// This routine blows the whistle when a crime is in progress            */
/* SFUN_WHISTLE                                                          */
/* int whistle(struct spec_arg *sarg)
{
    assert(sarg->fptr->data == NULL);
    if (sarg->cmd->no == CMD_AUTO_EXTRACT)
    {
        sarg->fptr->data = NULL;
        return SFR_SHARE;
    }

    if ((sarg->cmd->no < CMD_AUTO_COMBAT) || !sarg->activator || !IS_CHAR(sarg->activator))
        return SFR_SHARE;

    if (CHAR_POS(sarg->activator) < POSITION_STUNNED)
        return SFR_SHARE;

    if (CHAR_AWAKE(sarg->owner) && CHAR_COMBAT(sarg->activator) &&
        CHAR_CAN_SEE(sarg->owner, sarg->activator))
    {
        if (crime_in_progress(sarg->activator, CHAR_FIGHTING(sarg->activator)))
        {
            act("$1n blows in a small whistle!  'UUIIIIIIIHHHHH'",
                A_SOMEONE, sarg->owner, cActParameter(), sarg->activator, TO_ROOM);
            call_guards(sarg->owner);
            simple_one_hit(sarg->owner, sarg->activator);
            REMOVE_BIT(CHAR_FLAGS(sarg->activator), CHAR_SELF_DEFENCE);
            return SFR_SHARE;
        }
    }

    return SFR_SHARE;
}

// -------------------------------------------------------------- */

int reward_give(spec_arg *sarg)
{
    unit_data *u = nullptr;
    unit_affected_type *paf = nullptr;
    std::string buf;
    currency_t cur = 0;

    if (!is_command(sarg->cmd, "give"))
    {
        return SFR_SHARE;
    }

    u = sarg->owner->getUnitContains();

    buf = "give ";
    buf = buf + (char *)sarg->arg;
    command_interpreter(sarg->activator, (const char *)buf.c_str());

    if (sarg->owner->getUnitContains() == u)
    { /* Was it given nothing? */
        return SFR_BLOCK;
    }

    if ((paf = affected_by_spell(sarg->owner->getUnitContains(), ID_REWARD)) == nullptr)
    {
        act("$1n says, 'Thank you $3n, that is very nice of you.'", A_SOMEONE, sarg->owner, cActParameter(), sarg->activator, TO_ROOM);
        return SFR_BLOCK;
    }

    act("$1n says, '$3n, receieve this as a token of our gratitude.'", A_SOMEONE, sarg->owner, cActParameter(), sarg->activator, TO_ROOM);

    cur = local_currency(sarg->owner);

    if (sarg->activator->isPC())
    {
        gain_exp(sarg->activator, MIN(level_xp(CHAR_LEVEL(sarg->activator)), paf->getDataAtIndex(0)));
    }

    money_to_unit(sarg->activator, paf->getDataAtIndex(1), cur);

    extract_unit(sarg->owner->getUnitContains());

    return SFR_BLOCK;
}

/* int reward_board(struct spec_arg *sarg)
{
    class unit_data *u;
    class unit_affected_type *af = NULL;
    int found = FALSE;
    char buf[256];
    char *c = (char *)sarg->arg;

    if (!is_command(sarg->cmd, "look"))
        return SFR_SHARE;

    if (find_unit(sarg->activator, &c, 0, FIND_UNIT_SURRO) != sarg->owner)
        return SFR_SHARE;

    act("$1n looks at the board of rewards.",
        A_ALWAYS, sarg->activator, cActParameter(), cActParameter(), TO_ROOM);

    for (u = g_unit_list; u; u = u->gnext)
        if (IS_CHAR(u))
        {
            if ((af = affected_by_spell(u, ID_REWARD)))
            {
                found = TRUE;
                s printf(buf, "%s (%s) wanted dead for %d xp "
                             "and %s (%d crimes).<br/>",
                        UNIT_NAME(u),
                        IS_PC(u) ? "Player" : "Monster",
                        af->data[0],
                        money_string(af->data[1],
                                     local_currency(sarg->owner), FALSE),
                        af->data[2]);
                send_to_char(buf, sarg->activator);
            }
            else if (IS_SET(CHAR_FLAGS(u), CHAR_OUTLAW) &&
                     IS_SET(CHAR_FLAGS(u), CHAR_PROTECTED))
            {
                s printf(buf, "%s (%s) wanted alive for imprisonment.<br/>",
                        UNIT_NAME(u), IS_PC(u) ? "Player" : "Monster");
                send_to_char(buf, sarg->activator);
                found = TRUE;
            }
            else if (IS_SET(CHAR_FLAGS(u), CHAR_OUTLAW))
            {
                s printf(buf, "%s (%s) wanted dead or alive.<br/>",
                        UNIT_NAME(u), IS_PC(u) ? "Player" : "Monster");
                send_to_char(buf, sarg->activator);
                found = TRUE;
            }
        }

    if (!found)
        send_to_char("No rewards currently offered.<br/>", sarg->activator);

    return SFR_BLOCK;
}

void tif_reward_on(class unit_affected_type *af, class unit_data *unit)
{
    if (IS_CHAR(unit))
    {
        REMOVE_BIT(CHAR_FLAGS(unit), CHAR_PROTECTED);
        SET_BIT(CHAR_FLAGS(unit), CHAR_OUTLAW);

        if (CHAR_AWAKE(unit))
        {
            send_to_char("You feel wanted...<br/>", unit);
            act("$1n suddenly seems a little paranoid.",
                A_HIDEINV, unit, cActParameter(), cActParameter(), TO_ROOM);
        }
    }
    else
    {
        act("You realize that the $1N is perhaps worth something.",
            A_HIDEINV, unit, cActParameter(), cActParameter(), TO_ROOM);
    }
}

void tif_reward_off(class unit_affected_type *af, class unit_data *unit)
{
    if (IS_CHAR(unit))
        REMOVE_BIT(CHAR_FLAGS(unit), CHAR_OUTLAW);
}
*/

/* int new_crime_serial_no(void)
{
   FILE *file;

   // read next serial number to be assigned to crime
   if (!(file =
             fopen_cache(str_cc(g_cServerConfig.m_libdir, CRIME_NUM_FILE), "r+")))
   {
      slog(LOG_OFF, 0, "Can't open file 'crime-nr'");
      assert(FALSE);
   }

   rewind(file);
   int mstmp = fscanf(file, "%d", &crime_serial_no);
   if (mstmp < 0)
   {
      slog(LOG_ALL, 0, "%s: Unexpected bytes %d. Resetting crime number to zero", CRIME_NUM_FILE, mstmp);
      crime_serial_no = 0;
   }

   crime_serial_no++;
   rewind(file);
   mstmp = fprintf(file, "%d", crime_serial_no);
   if (mstmp < 1)
   {
      slog(LOG_ALL, 0, "ERROR: Unexpected bytes in new_crime_serial_no");
      assert(FALSE);
   }

   fflush(file);

   // No fclose(file), using cache

   return crime_serial_no;
}*/

// -------------------------------------------------------------- */

/* void boot_justice(void)
{
    touch_file(str_cc(g_cServerConfig.m_libdir, CRIME_NUM_FILE));
    touch_file(str_cc(g_cServerConfig.m_libdir, CRIME_ACCUSE_FILE));
    } */
