/*
 $Author: All $
 $RCSfile: handler.cpp,v $
 $Date: 2005/06/28 20:17:48 $
 $Revision: 2.9 $
 */
#include "handler.h"

#include "affect.h"
#include "comm.h"
#include "db.h"
#include "dilrun.h"
#include "files.h"
#include "interpreter.h"
#include "main_functions.h"
#include "mobact.h"
#include "money.h"
#include "nanny.h"
#include "slog.h"
#include "spec_assign.h"
#include "utils.h"
#include "zon_basis.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

class descriptor_data *unit_is_edited(class unit_data *u)
{
    class descriptor_data *d = nullptr;

    for (d = g_descriptor_list; d; d = d->next)
    {
        if (d->editing == u)
        {
            return d;
        }
    }

    return nullptr;
}

/* By using this, we can easily sort the list if ever needed */
void insert_in_unit_list(class unit_data *u)
{
    assert(u->gnext == nullptr && u->gprevious == nullptr && g_unit_list != u);

    if (UNIT_FILE_INDEX(u))
    {
        UNIT_FILE_INDEX(u)->fi_unit_list.push_front(u);
    }

    class unit_data *tmp_u = nullptr;

    if (!g_unit_list)
    {
        u->gnext = nullptr;
        u->gprevious = nullptr;
        g_unit_list = u;
        g_npc_head = u;
        g_room_head = u;
        g_obj_head = u;
        return;
    }

    switch (UNIT_TYPE(u))
    {
        case UNIT_ST_PC:
        {
            tmp_u = g_unit_list;
            u->gnext = tmp_u;
            u->gprevious = tmp_u->gprevious;
            tmp_u->gprevious = u;

            if (tmp_u == g_unit_list)
            {
                g_unit_list = u;
            }
            break;
        }
        case UNIT_ST_NPC:
        {
            if (UNIT_TYPE(g_npc_head) != UNIT_ST_NPC)
            {
                tmp_u = g_unit_list;
                for (; tmp_u && IS_PC(tmp_u); tmp_u = tmp_u->gnext)
                {
                    ;
                }
            }
            else
            {
                tmp_u = g_npc_head;
            }

            u->gnext = tmp_u;
            u->gprevious = tmp_u->gprevious;
            if (tmp_u->gprevious)
            {
                tmp_u->gprevious->gnext = u;
            }
            tmp_u->gprevious = u;

            if (tmp_u == g_unit_list)
            {
                g_unit_list = u;
            }
            g_npc_head = u;
            break;
        }
        case UNIT_ST_OBJ:
        {
            if (UNIT_TYPE(g_obj_head) != UNIT_ST_OBJ)
            {
                tmp_u = g_unit_list;
                for (; tmp_u && IS_CHAR(tmp_u); tmp_u = tmp_u->gnext)
                {
                    ;
                }
            }
            else
            {
                tmp_u = g_obj_head;
            }

            u->gnext = tmp_u;
            u->gprevious = tmp_u->gprevious;
            if (tmp_u->gprevious)
            {
                tmp_u->gprevious->gnext = u;
            }
            tmp_u->gprevious = u;

            if (tmp_u == g_unit_list)
            {
                g_unit_list = u;
            }
            g_obj_head = u;
            break;
        }
        case UNIT_ST_ROOM:
        {
            if (UNIT_TYPE(g_room_head) != UNIT_ST_ROOM)
            {
                tmp_u = g_unit_list;
                for (; tmp_u && (IS_CHAR(tmp_u) || IS_OBJ(tmp_u)); tmp_u = tmp_u->gnext)
                {
                    ;
                }
            }
            else
            {
                tmp_u = g_room_head;
            }

            u->gnext = tmp_u;
            u->gprevious = tmp_u->gprevious;
            if (tmp_u->gprevious)
            {
                tmp_u->gprevious->gnext = u;
            }
            tmp_u->gprevious = u;

            if (tmp_u == g_unit_list)
            {
                g_unit_list = u;
            }
            g_room_head = u;
            break;
        }
    }
}

/* Remove a unit from the g_unit_list */
void remove_from_unit_list(class unit_data *unit)
{
    assert(unit->gprevious || unit->gnext || (g_unit_list == unit));

    if (UNIT_FILE_INDEX(unit))
    {
        UNIT_FILE_INDEX(unit)->fi_unit_list.remove(unit);
    }

    if (g_npc_head == unit)
    {
        g_npc_head = unit->gnext;
    }
    if (g_obj_head == unit)
    {
        g_obj_head = unit->gnext;
    }
    if (g_room_head == unit)
    {
        g_room_head = unit->gnext;
    }
    if (g_unit_list == unit)
    {
        g_unit_list = unit->gnext;
    }
    else
    { /* Then this is always true 'if (unit->gprevious)'  */
        unit->gprevious->gnext = unit->gnext;
    }

    if (unit->gnext)
    {
        unit->gnext->gprevious = unit->gprevious;
    }

    unit->gnext = unit->gprevious = nullptr;
}

class unit_fptr *find_fptr(class unit_data *u, ubit16 idx)
{
    class unit_fptr *tf = nullptr;

    for (tf = UNIT_FUNC(u); tf; tf = tf->next)
    {
        if (tf->index == idx)
        {
            return tf;
        }
    }

    return nullptr;
}

// 2020: Add it prioritized
void insert_fptr(class unit_data *u, class unit_fptr *f)
{
    if (f->priority == 0)
    {
        slog(LOG_ALL, 0, "fptr_priotity not set, setting to chores");
        f->priority = FN_PRI_CHORES;
    }

    // If there are no funcs, just add it.
    if (UNIT_FUNC(u) == nullptr)
    {
        f->next = UNIT_FUNC(u);
        UNIT_FUNC(u) = f;
        return;
    }

    // See if we are higher priority than head element
    if (f->priority < UNIT_FUNC(u)->priority)
    {
        f->next = UNIT_FUNC(u);
        UNIT_FUNC(u) = f;
        return;
    }

    class unit_fptr *p = nullptr;
    class unit_fptr *prev = nullptr;

    // Find location to insert
    prev = UNIT_FUNC(u);
    for (p = UNIT_FUNC(u)->next; p; p = p->next)
    {
        if (f->priority < p->priority)
        {
            f->next = p;
            prev->next = f;
            return;
        }
        prev = p;
    }

    // We are the lowest priority, insert at the
    assert(prev->next == nullptr);
    prev->next = f;
    f->next = nullptr;
}

class unit_fptr *create_fptr(class unit_data *u, ubit16 index, ubit16 priority, ubit16 beat, ubit16 flags, void *data)
{
    class unit_fptr *f = nullptr;
    f = new EMPLACE(unit_fptr) unit_fptr;

    assert(f);
    assert(!f->is_destructed());

    f->index = index;
    f->priority = priority;
    f->heart_beat = beat;
    f->flags = flags;
    f->data = data;
    f->event = nullptr;

    membug_verify(f->data);
    insert_fptr(u, f);

    start_special(u, f);

    membug_verify(f);
    membug_verify(f->data);

    return f;
}

/* Does not free 'f' - it is done by clear_destruct by comm.c */
void destroy_fptr(class unit_data *u, class unit_fptr *f)
{
    class unit_fptr *tf = nullptr;
    struct spec_arg sarg;

    assert(f);
    if (f->is_destructed())
    {
        return;
    }

    assert(!f->is_destructed());

    f->register_destruct();

#ifdef DEBUG_HISTORY
    add_func_history(u, f->index, 0);
#endif

    sarg.owner = u;
    sarg.activator = nullptr;
    sarg.medium = nullptr;
    sarg.target = nullptr;
    sarg.pInt = nullptr;
    sarg.fptr = f;
    sarg.cmd = &g_cmd_auto_extract;
    sarg.arg = "";
    sarg.mflags = ((ubit16)0);

    (*(g_unit_function_array[f->index].func))(&sarg);

    /* Data is free'ed in destruct() if it is not NULL now */

    membug_verify(f);
    stop_special(u, f);
    membug_verify(f);

    /* Only unlink function, do not free it! */
    if (UNIT_FUNC(u) == f)
    {
        UNIT_FUNC(u) = f->next;
    }
    else
    {
        for (tf = UNIT_FUNC(u); tf && (tf->next != f); tf = tf->next)
        {
            ;
        }
        if (tf)
        {
            assert(tf->next == f);
            tf->next = f->next;
        }
    }

    f->next = nullptr;
    assert(f->event == nullptr);
}

/* Stop the 'ch' from following his master    */
/* Call die_follower if a person dies         */
void stop_following(class unit_data *ch)
{
    struct char_follow_type *j = nullptr;
    struct char_follow_type *k = nullptr;

    assert(CHAR_MASTER(ch));

    if (CHAR_FOLLOWERS(CHAR_MASTER(ch))->follower == ch) /* Head of list? */
    {
        k = CHAR_FOLLOWERS(CHAR_MASTER(ch));
        CHAR_FOLLOWERS(CHAR_MASTER(ch)) = k->next;
        FREE(k);
    }
    else
    { /* locate follower who is not head of list */
        for (k = CHAR_FOLLOWERS(CHAR_MASTER(ch)); k->next->follower != ch; k = k->next)
        {
            ;
        }
        j = k->next;
        k->next = j->next;
        FREE(j);
    }

    CHAR_MASTER(ch) = nullptr;

    send_done(ch, nullptr, nullptr, 0, g_cmd_follow, "");
}

/* Set 'ch' to follow leader. Circles allowed. */
void start_following(class unit_data *ch, class unit_data *leader)
{
    struct char_follow_type *k = nullptr;

    assert(!leader->is_destructed());
    assert(!ch->is_destructed());

    REMOVE_BIT(CHAR_FLAGS(ch), CHAR_GROUP);
    if (CHAR_MASTER(ch))
    {
        stop_following(ch);
    }
    CHAR_MASTER(ch) = leader;
    CREATE(k, struct char_follow_type, 1);
    k->follower = ch;
    k->next = CHAR_FOLLOWERS(leader);
    CHAR_FOLLOWERS(leader) = k;

    send_done(ch, nullptr, leader, 0, g_cmd_follow, "");
}

/* Called by extract_unit when a character that follows/is followed dies */
void die_follower(class unit_data *ch)
{
    struct char_follow_type *j = nullptr;
    struct char_follow_type *k = nullptr;

    if (CHAR_MASTER(ch))
    {
        stop_following(ch);
    }

    for (k = CHAR_FOLLOWERS(ch); k; k = j)
    {
        j = k->next;
        stop_following(k->follower);
    }
}

/*
   An NPC can see if the sum of this is >= 0

           Lights of the unit itself
    plus   Light if it is daylight (depends on time of day)
    plus   Lights of the unit it is inside

ubit1 unit_islight(class unit_data *u)
{
    if (!u)
        return 0;

    return (UNIT_LIGHTS(unit) + UNIT_OUTSIDE_LIGHT(unit) + ((UNIT_IN(unit)?UNIT_LIGHTS(UNIT_IN(unit)):0)) >= 0;
}
*/

/* MS2020. Trying to remember how this worked:

    BRIGHT: How much a unit shines wtih light/dark afecting what it is in
    LIGHTS: How much light is inside a unit (not how much it shines out)
    ILLUM:

Example: (Lights, Bright, Illum)
    Room: (-1,0,0)
    NPC in room: (0,0,0)
    Lantern in room: (0,0,0)
    Coffin in room (0,0,0)
    Lantern is lit (B:+2, 0, 0)
       Room now has L:1, B:0
    NPC takes lantern. Room: L-1. NPC: L:2, B:0
    NPC equips lantern Room: L+1. NPC: L:2, B:2

    What about NPC in coffin when it is open & closed?
    What about room in room when it is open & closed? (barrel)

    coffin@midgaard & horse@midgaard are both transparent

    Coffin (0,0,0)
       Cast light coffin (0,B1,0)
    If you cast light on a coffin, the coffin shines (bright).
    But if you crawl inside and close the lid it is still dark.


*/
/* Call this routine if you modify the brightness of a unit */
/* in order to correctly update the environment of the unit */
void modify_bright(class unit_data *unit, int bright)
{
    class unit_data *ext = nullptr;
    class unit_data *in = nullptr;

    UNIT_BRIGHT(unit) += bright;

    if ((in = UNIT_IN(unit)))
    { /* Light up what the unit is inside */
        UNIT_LIGHTS(in) += bright;
    }

    if (IS_OBJ(unit) && OBJ_EQP_POS(unit))
    {
        /* The char holding the torch light up the SAME way the torch does! */
        /* this works with the equib/unequib functions. This is NOT a case  */
        /* of transparancy.  */
        modify_bright(in, bright);
    }
    else if (in && UNIT_IS_TRANSPARENT(in))
    {
        /* the unit is inside a transperant unit, so it lights up too */
        /* this works with actions in unit-up/down                    */
        UNIT_BRIGHT(in) += bright;
        if ((ext = UNIT_IN(in)))
        {
            UNIT_LIGHTS(ext) += bright;
            UNIT_ILLUM(in) += bright;
        }
    }
}

void trans_set(class unit_data *u)
{
    class unit_data *u2 = nullptr;
    int sum = 0;

    for (u2 = UNIT_CONTAINS(u); u2; u2 = u2->next)
    {
        sum += UNIT_BRIGHT(u2);
    }

    UNIT_ILLUM(u) = sum;
    UNIT_BRIGHT(u) += sum;

    if (UNIT_IN(u))
    {
        UNIT_LIGHTS(UNIT_IN(u)) += sum;
    }
}

void trans_unset(class unit_data *u)
{
    UNIT_BRIGHT(u) -= UNIT_ILLUM(u);

    if (UNIT_IN(u))
    {
        UNIT_LIGHTS(UNIT_IN(u)) -= UNIT_ILLUM(u);
    }

    UNIT_ILLUM(u) = 0;
}

class unit_data *equipment(class unit_data *ch, ubit8 pos)
{
    class unit_data *u = nullptr;

    assert(IS_CHAR(ch));

    for (u = UNIT_CONTAINS(ch); u; u = u->next)
    {
        if (IS_OBJ(u) && pos == OBJ_EQP_POS(u))
        {
            return u;
        }
    }

    return nullptr;
}

/* The following functions find armor / weapons on a person with     */
/* type checks (i.e. trash does not protect!).                       */
class unit_data *equipment_type(class unit_data *ch, int pos, ubit8 type)
{
    class unit_data *obj = nullptr;

    obj = equipment(ch, pos);

    if (obj && OBJ_TYPE(obj) == type)
    {
        return obj;
    }
    else
    {
        return nullptr;
    }
}

void equip_char(class unit_data *ch, class unit_data *obj, ubit8 pos)
{
    class unit_affected_type *af = nullptr;
    class unit_affected_type newaf;

    assert(pos > 0 && IS_OBJ(obj) && IS_CHAR(ch));
    assert(!equipment(ch, pos));
    assert(UNIT_IN(obj) == ch); /* Must carry object in inventory */

    OBJ_EQP_POS(obj) = pos;
    modify_bright(ch, UNIT_BRIGHT(obj)); /* Update light sources */

    for (af = UNIT_AFFECTED(obj); af; af = af->next)
    {
        if (af->id < 0) /* It is a transfer affect! */
        {
            newaf = *af;
            newaf.id = -newaf.id; /* No longer a transfer    */
            newaf.duration = -1;  /* Permanent until unequip */
            create_affect(ch, &newaf);
        }
    }
}

class unit_data *unequip_object(class unit_data *obj)
{
    class unit_data *ch = nullptr;
    class unit_affected_type *af = nullptr;
    class unit_affected_type *caf = nullptr;

    ch = UNIT_IN(obj);

    assert(IS_OBJ(obj) && OBJ_EQP_POS(obj));
    assert(IS_CHAR(ch));

    OBJ_EQP_POS(obj) = 0;
    modify_bright(ch, -UNIT_BRIGHT(obj)); /* Update light sources */

    for (af = UNIT_AFFECTED(obj); af; af = af->next)
    {
        if (af->id < 0) /* It is a transfer affect! */
        {
            for (caf = UNIT_AFFECTED(ch); caf; caf = caf->next)
            {
                if ((-caf->id == af->id) && (caf->duration == -1) && (caf->data[0] == af->data[0]) && (caf->data[1] == af->data[1]) &&
                    // THIS IS NOT TESTED! (caf->data[2] == af->data[2]) &&
                    (caf->applyf_i == af->applyf_i) && (caf->firstf_i == af->firstf_i) && (caf->lastf_i == af->lastf_i) &&
                    (caf->tickf_i == af->tickf_i))
                {
                    destroy_affect(caf);
                    break; /* Skip inner for loop since we found the affect */
                }
            }
        }
    }

    return obj;
}

/* Checks if a unit which is to be put inside another unit will pass */
/* through itself. For example if Papi carries a coffin and God is   */
/* inside the coffin, when God types trans Papi this function will   */
/* return TRUE                                                       */
int unit_recursive(class unit_data *from, class unit_data *to)
{
    class unit_data *u = nullptr;

    for (u = to; u; u = UNIT_IN(u))
    {
        if (u == from)
        {
            return TRUE;
        }
    }

    return FALSE;
}

class zone_type *unit_zone(const class unit_data *unit)
{
    class unit_data *org = (class unit_data *)unit;

    for (; unit; unit = UNIT_IN(unit))
    {
        if (!UNIT_IN(unit))
        {
            //      assert(IS_ROOM(unit));
            if (!IS_ROOM(unit))
            {
                slog(LOG_ALL, 0, "ZONE: FATAL(1): %s@%s IN NO ROOMS WHILE NOT A ROOM!!", UNIT_FI_NAME(org), UNIT_FI_ZONENAME(org));
                return nullptr;
            }
            return UNIT_FILE_INDEX(unit)->zone;
        }
    }

    slog(LOG_ALL, 0, "ZONE: FATAL(2): %s@%s IN NO ROOMS WHILE NOT A ROOM!!", UNIT_FI_NAME(org), UNIT_FI_ZONENAME(org));
    return nullptr;
}

//
// Returns a symname text string of all the units a unit is in
//
std::string unit_trace_up(class unit_data *unit)
{
    class unit_data *u = nullptr;
    std::string s;
    std::string t;

    s = "";
    s.append(UNIT_FI_NAME(unit));
    s.append("@");
    s.append(UNIT_FI_ZONENAME(unit));

    for (u = UNIT_IN(unit); u; u = UNIT_IN(u))
    {
        t = " in ";
        t.append(UNIT_FI_NAME(u));
        t.append("@");
        t.append(UNIT_FI_ZONENAME(u));
        s.append(t);
    }

    return s;
}

class unit_data *unit_room(class unit_data *unit)
{
    if (unit == nullptr)
    {
        return nullptr;
    }

    class unit_data *org = unit;

    for (; unit; unit = UNIT_IN(unit))
    {
        if (IS_ROOM(unit))
        {
            return unit;
        }
    }

    slog(LOG_ALL, 0, "ROOM: FATAL(3): %s@%s IN NO ROOMS WHILE NOT A ROOM!!", UNIT_FI_NAME(org), UNIT_FI_ZONENAME(org));
    return nullptr;
}

void intern_unit_up(class unit_data *unit, ubit1 pile)
{
    class unit_data *u = nullptr;
    class unit_data *in = nullptr;
    class unit_data *toin = nullptr;
    class unit_data *extin = nullptr;
    sbit8 bright = 0;
    sbit8 selfb = 0;

    assert(UNIT_IN(unit));

    /* resolve *ALL* light!!! */
    in = UNIT_IN(unit);                     /* where to move unit up to */
    toin = UNIT_IN(in);                     /* unit around in           */
    extin = toin ? UNIT_IN(toin) : nullptr; /* unit around toin         */
    bright = UNIT_BRIGHT(unit);             /* brightness inc. trans    */
    selfb = bright - UNIT_ILLUM(unit);      /* brightness excl. trans   */

    UNIT_LIGHTS(in) -= bright; /* Subtract Light */
    if (UNIT_IS_TRANSPARENT(in))
    {
        UNIT_ILLUM(in) -= selfb;
        UNIT_BRIGHT(in) -= selfb;
    }
    else if (toin)
    {
        UNIT_LIGHTS(toin) += bright;
    }

    if (toin && UNIT_IS_TRANSPARENT(toin))
    {
        UNIT_BRIGHT(toin) += selfb;
        UNIT_ILLUM(toin) += selfb;
        if (extin)
        {
            UNIT_LIGHTS(extin) += selfb;
        }
    }

    if (IS_CHAR(unit))
    {
        --UNIT_CHARS(UNIT_IN(unit));
    }
    /*fuck*/
    UNIT_WEIGHT(UNIT_IN(unit)) -= UNIT_WEIGHT(unit);

    if (unit == UNIT_CONTAINS(UNIT_IN(unit)))
    {
        UNIT_CONTAINS(UNIT_IN(unit)) = unit->next;
    }
    else
    {
        for (u = UNIT_CONTAINS(UNIT_IN(unit)); u->next != unit; u = u->next)
        {
            ;
        }
        u->next = unit->next;
    }

    unit->next = nullptr;

    if ((UNIT_IN(unit) = UNIT_IN(UNIT_IN(unit))))
    {
        unit->next = UNIT_CONTAINS(UNIT_IN(unit));
        UNIT_CONTAINS(UNIT_IN(unit)) = unit;
        if (IS_CHAR(unit))
        {
            ++UNIT_CHARS(UNIT_IN(unit));
        }
    }

    if (pile && IS_MONEY(unit) && UNIT_IN(unit))
    {
        pile_money(unit);
    }
}

void unit_up(class unit_data *unit)
{
    intern_unit_up(unit, TRUE);
}

void unit_from_unit(class unit_data *unit)
{
    while (UNIT_IN(unit))
    {
        intern_unit_up(unit, FALSE);
    }
}

void intern_unit_down(class unit_data *unit, class unit_data *to, ubit1 pile)
{
    class unit_data *u = nullptr;
    class unit_data *in = nullptr;
    class unit_data *extin = nullptr;
    sbit8 bright = 0;
    sbit8 selfb = 0;

    assert(UNIT_IN(unit) == UNIT_IN(to));
    assert(unit != to);

    /* do *ALL* light here!!!! */
    in = UNIT_IN(unit);
    extin = in ? UNIT_IN(in) : nullptr;
    bright = UNIT_BRIGHT(unit);
    selfb = bright - UNIT_ILLUM(unit);

    UNIT_LIGHTS(to) += bright;
    if (UNIT_IS_TRANSPARENT(to))
    {
        UNIT_BRIGHT(to) += selfb;
        UNIT_ILLUM(to) += selfb;
    }
    else if (in)
    {
        UNIT_LIGHTS(in) -= bright;
    }

    if (in && UNIT_IS_TRANSPARENT(in))
    {
        UNIT_BRIGHT(in) -= selfb;
        UNIT_ILLUM(in) -= selfb;
        if (extin)
        {
            UNIT_LIGHTS(extin) -= selfb;
        }
    }

    if (UNIT_IN(unit))
    {
        if (IS_CHAR(unit))
        {
            --UNIT_CHARS(UNIT_IN(unit));
        }
        if (unit == UNIT_CONTAINS(UNIT_IN(unit)))
        {
            UNIT_CONTAINS(UNIT_IN(unit)) = unit->next;
        }
        else
        {
            for (u = UNIT_CONTAINS(UNIT_IN(unit)); u->next != unit; u = u->next)
            {
                ;
            }
            u->next = unit->next;
        }
    }

    UNIT_IN(unit) = to;
    unit->next = UNIT_CONTAINS(to);
    UNIT_CONTAINS(to) = unit;

    if (IS_CHAR(unit))
    {
        ++UNIT_CHARS(UNIT_IN(unit));
    }
    UNIT_WEIGHT(to) += UNIT_WEIGHT(unit);

    if (pile && IS_MONEY(unit))
    {
        pile_money(unit);
    }
}

void unit_down(class unit_data *unit, class unit_data *to)
{
    intern_unit_down(unit, to, TRUE);
}

void intern_unit_to_unit(class unit_data *unit, class unit_data *to, ubit1 pile)
{
    assert(to);
    if (unit == to)
    {
        slog(LOG_ALL, 0, "ERROR.  Unit tried to link into its self, please report.");
        assert(to != unit);
    }

    if (UNIT_IN(to))
    {
        intern_unit_to_unit(unit, UNIT_IN(to), FALSE);
    }

    intern_unit_down(unit, to, FALSE);

    if (pile && IS_MONEY(unit))
    {
        pile_money(unit);
    }
}

void unit_to_unit(class unit_data *unit, class unit_data *to)
{
    intern_unit_to_unit(unit, to, TRUE);
}

void snoop(class unit_data *ch, class unit_data *victim)
{
    assert(!victim->is_destructed());
    assert(!ch->is_destructed());

    assert(ch != victim);
    /*   assert(IS_PC(ch) && IS_PC(victim)); */
    assert(CHAR_DESCRIPTOR(ch) && CHAR_DESCRIPTOR(victim));
    assert(!CHAR_IS_SNOOPING(ch) && !CHAR_IS_SNOOPED(victim));
    assert(CHAR_LEVEL(CHAR_ORIGINAL(victim)) < CHAR_LEVEL(CHAR_ORIGINAL(ch)));
    /*   assert(!CHAR_IS_SWITCHED(victim)); */

    CHAR_DESCRIPTOR(ch)->snoop.snooping = victim;
    CHAR_DESCRIPTOR(victim)->snoop.snoop_by = ch;
}

/* Mode 0: Stop ch from snooping a person       */
/* Mode 1: Mode 0 + stop any person snooping ch */
void unsnoop(class unit_data *ch, int mode)
{
    assert(CHAR_DESCRIPTOR(ch));
    assert(CHAR_IS_SNOOPING(ch) || CHAR_IS_SNOOPED(ch));

    if (CHAR_IS_SNOOPING(ch))
    {
        act("You no longer snoop $3n.", A_SOMEONE, ch, cActParameter(), CHAR_DESCRIPTOR(ch)->snoop.snooping, TO_CHAR);
        CHAR_DESCRIPTOR(CHAR_DESCRIPTOR(ch)->snoop.snooping)->snoop.snoop_by = nullptr;
        CHAR_DESCRIPTOR(ch)->snoop.snooping = nullptr;
    }

    if (CHAR_IS_SNOOPED(ch) && mode)
    {
        act("You no longer snoop $3n, $3e was extracted.", A_SOMEONE, CHAR_DESCRIPTOR(ch)->snoop.snoop_by, cActParameter(), ch, TO_CHAR);
        CHAR_DESCRIPTOR(CHAR_DESCRIPTOR(ch)->snoop.snoop_by)->snoop.snooping = nullptr;
        CHAR_DESCRIPTOR(ch)->snoop.snoop_by = nullptr;
    }
}

void switchbody(class unit_data *ch, class unit_data *vict)
{
    assert(CHAR_DESCRIPTOR(ch) && IS_NPC(vict));
    assert(!CHAR_DESCRIPTOR(vict));
    assert(!CHAR_IS_SNOOPING(ch) || CHAR_DESCRIPTOR(CHAR_IS_SNOOPING(ch)));
    assert(!CHAR_IS_SNOOPED(ch) || CHAR_DESCRIPTOR(CHAR_IS_SNOOPED(ch)));
    assert(!vict->is_destructed());
    assert(!ch->is_destructed());

    CHAR_DESCRIPTOR(ch)->character = vict;

    if (IS_PC(ch))
    {
        CHAR_DESCRIPTOR(ch)->original = ch;
    }
    if (CHAR_IS_SNOOPING(ch))
    {
        CHAR_DESCRIPTOR(CHAR_DESCRIPTOR(ch)->snoop.snooping)->snoop.snoop_by = vict;
    }
    if (CHAR_IS_SNOOPED(ch))
    {
        CHAR_DESCRIPTOR(CHAR_DESCRIPTOR(ch)->snoop.snoop_by)->snoop.snooping = vict;
    }

    CHAR_DESCRIPTOR(vict) = CHAR_DESCRIPTOR(ch);
    CHAR_DESCRIPTOR(ch) = nullptr;
    CHAR_LAST_ROOM(vict) = nullptr;
}

void unswitchbody(class unit_data *npc)
{
    assert(IS_NPC(npc) && CHAR_DESCRIPTOR(npc));
    assert(CHAR_IS_SWITCHED(npc));
    assert(!CHAR_IS_SNOOPING(npc) || CHAR_DESCRIPTOR(CHAR_IS_SNOOPING(npc)));
    assert(!CHAR_IS_SNOOPED(npc) || CHAR_DESCRIPTOR(CHAR_IS_SNOOPED(npc)));

    send_to_char("You return to your original body.<br/>", npc);

    if (CHAR_IS_SNOOPING(npc))
    {
        CHAR_DESCRIPTOR(CHAR_DESCRIPTOR(npc)->snoop.snooping)->snoop.snoop_by = CHAR_ORIGINAL(npc);
    }

    if (CHAR_IS_SNOOPED(npc))
    {
        CHAR_DESCRIPTOR(CHAR_DESCRIPTOR(npc)->snoop.snoop_by)->snoop.snooping = CHAR_ORIGINAL(npc);
    }

    CHAR_DESCRIPTOR(npc)->character = CHAR_ORIGINAL(npc);
    CHAR_DESCRIPTOR(npc)->original = nullptr;

    CHAR_DESCRIPTOR(CHAR_DESCRIPTOR(npc)->character) = CHAR_DESCRIPTOR(npc);
    CHAR_DESCRIPTOR(npc) = nullptr;
}

void stop_fightfollow(unit_data *unit)
{
    if (IS_CHAR(unit))
    {
        if (CHAR_FOLLOWERS(unit) || CHAR_MASTER(unit))
        {
            die_follower(unit);
        }

        stop_fighting(unit);
    }
}

void stop_snoopwrite(unit_data *unit)
{
    // removed this statement: if (!IS_PC(unit) || UNIT_IN(unit))
    //

    class descriptor_data *d = nullptr;
    while ((d = unit_is_edited(unit)))
    {
        send_to_char("<br/>Unit was extracted, sorry.<br/>", d->character);
        set_descriptor_fptr(d, descriptor_interpreter, FALSE);
    }

    if (IS_CHAR(unit))
    {
        if (CHAR_IS_SWITCHED(unit))
        {
            unswitchbody(unit);
        }

        /* If the PC which is switched is extracted, then unswitch */
        if (IS_PC(unit) && !CHAR_DESCRIPTOR(unit))
        {
            for (d = g_descriptor_list; d; d = d->next)
            {
                if (d->original == unit)
                {
                    unswitchbody(d->character);
                    break;
                }
            }
        }

        if (CHAR_IS_SNOOPING(unit) || CHAR_IS_SNOOPED(unit))
        {
            unsnoop(unit, 1); /* Remove all snoopers */
        }
    }
}

/* Used when a unit is to be extracted from the game */
/* Extracts recursively                              */
void extract_unit(class unit_data *unit)
{
    /* Prevent recursive calling on extracted units. */
    /* This happens on for example corpses. When the */
    /* destruct_affect is called inside extract we   */
    /* got a recursive call.                         */

    if (unit->is_destructed())
    {
        return;
    }

    /* We can't extract rooms! Sanity, MS 300595, wierd bug... */
    assert(!IS_ROOM(unit));

    if (IS_PC(unit))
    {
        // slog(LOG_ALL, 0, "DEBUG: Extracting player %s", UNIT_NAME(unit));
        UPC(unit)->gstate_tomenu(nullptr);
    }

    DeactivateDil(unit);

    /*if (IS_PC(unit))
    {
        slog(LOG_ALL, 0, "DEBUG: Extracting player step 2: %s", UNIT_NAME(unit));
    }*/

    unit->register_destruct();

    if (UNIT_IS_EQUIPPED(unit))
    {
        unequip_object(unit);
    }

    stop_all_special(unit);
    stop_affect(unit);

    while (UNIT_CONTAINS(unit))
    {
        extract_unit(UNIT_CONTAINS(unit));
    }

    /*	void unlink_affect(class unit_affected_type *af);
          while (UNIT_FUNC(unit))
        destroy_fptr(unit, UNIT_FUNC(unit));

          while (UNIT_AFFECTED(unit))
             unlink_affect(UNIT_AFFECTED(unit));
    */

    stop_fightfollow(unit);
    stop_snoopwrite(unit);

    if (IS_PC(unit) && CHAR_DESCRIPTOR(unit))
    {
        UPC(unit)->disconnect_game();
    }

    /*if (!IS_PC(unit) || UNIT_IN(unit))
    {
      if (UNIT_IN(unit))
         unit_from_unit(unit);

      unit_to_unit(unit, g_destroy_room);

      // Otherwise find_unit will find it AFTER it has been extracted!!
      remove_from_unit_list(unit);
    }*/

    if (UNIT_IN(unit))
    {
        unit_from_unit(unit);
    }

    if (!IS_PC(unit))
    {
        unit_to_unit(unit, g_destroy_room); // Apparently dont place PCs in the destroy room

        // Otherwise find_unit will find it AFTER it has been extracted!!
        // Players are already removed from the list in gstate_tomenu()
        remove_from_unit_list(unit);
    }
}

/* ***********************************************************************
   Here follows high-level versions of some earlier routines, ie functions
   which incorporate the actual player-data.
   *********************************************************************** */

/* Add weight to the unit and change everything it is in */
/* (It will not change the -basic- weight of a player)   */
void weight_change_unit(class unit_data *unit, int weight)
{
    for (; unit; unit = UNIT_IN(unit))
    {
        UNIT_WEIGHT(unit) += weight;
    }
}

class extra_descr_data *quest_add(class unit_data *ch, const char *name, const char *descr)
{
    assert(name != nullptr);
    assert(name[0]);

    return PC_QUEST(ch).add(name, descr);
}
