/*
 $Author: All $
 $RCSfile: modify.cpp,v $
 $Date: 2005/06/28 20:17:48 $
 $Revision: 2.6 $
 */
/* Per https://sourceforge.net/p/predef/wiki/OperatingSystems/, this identifies
 *  Mac OS X. This is needed since OS X doesn't have crypt.h and instead uses
 *  unistd.h for these mappings. */
#if defined __APPLE__ && __MACH__
    #include <unistd.h>
#elif defined LINUX
    #include <crypt.h>
#endif

#include "affect.h"
#include "comm.h"
#include "common.h"
#include "constants.h"
#include "db.h"
#include "formatter.h"
#include "handler.h"
#include "interpreter.h"
#include "money.h"
#include "nanny.h"
#include "skills.h"
#include "slog.h"
#include "textutil.h"
#include "utils.h"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define UT_ROOM (1 << 0)
#define UT_OBJ (1 << 1)
#define UT_PC (1 << 2)
#define UT_NPC (1 << 3)
#define UT_CHAR (UT_NPC | UT_PC)
#define UT_UNIT (UT_CHAR | UT_OBJ | UT_ROOM)

#define AT_VAL 1     /* A value is expected as argument */
#define AT_BIT 2     /* A list of bits is expected as argument */
#define AT_TYP 3     /* A type (char) is expected as argument */
#define AT_STR 4     /* A single line string is expected as argument */
#define AT_DES 5     /* A multi line string is expected as argument */
#define AT_UNT 6     /* A unit-path is expected as argument */
#define AT_KEYDES 7  /* A keyword and a multiline descr. is expected */
#define AT_TYPVAL 8  /* A type followed by a value is expected */
#define AT_DIRBIT 9  /* A direction then a list of bits is expected */
#define AT_TYPDES 10 /* A type then a description is expected */
#define AT_DIRSTR 11 /* A direction then a string is expected */
#define AT_DIRUNT 12 /* A direction then a unit-path is expected */
#define AT_DIRDES 13 /* A direction then a description is expected */

#define MAX_SET_FIELDS 70

struct field_type
{
    ubit8 utype;            /* type of unit to work on           */
    ubit8 atype;            /* type of argument to expect        */
    const char **structure; /* structure of bit/type recognition */
    ubit8 minself;          /* minimum level to modify self      */
    ubit8 minother;         /* minimum level to modify other     */
    ubit8 minplayer;        /* minimum level to modify player    */
};

static const char *unit_field_names[MAX_SET_FIELDS + 1] = {
    "add-name",  "del-name",     "title",          "descr",         "add-extra", "del-extra",    "manipulate",   "flags",
    "weight",    "capacity",     "max-hit",        "hit",           "key",       "alignment",    "open-flags",   "toughness",
    "lights",    "bright",       "room-flags",     "movement",      "ccinfo",    "add-dir-name", "del-dir-name", "dir-flags",
    "dir-key",   "value0",       "value1",         "value2",        "value3",    "value4",       "obj-flags",    "cost",
    "rent",      "type",         "equip",          "guild-name",    "pwd",       "pc-flags",     "crimes",       "drunk",
    "full",      "thirsty",      "default-pos",    "npc-flags",     "hometown",  "exp",          "char-flags",   "mana",
    "endurance", "attack-type",  "hand-quality",   "size",          "race",      "sex",          "level",        "position",
    "ability",   "skill-points", "ability-points", "remove-affect", "add-quest", "del-quest",    "speed",        "add-info",
    "del-info",  "access",       "promptstr",      "age",           "lifespan",  "profession",   nullptr};

// These are oddly placed here because they need to initialize before used below
skill_collection g_AbiColl(ABIL_TREE_MAX + 1);
skill_collection g_WpnColl(WPN_TREE_MAX + 1);
skill_collection g_SkiColl(SKI_TREE_MAX + 1);
skill_collection g_SplColl(SPL_TREE_MAX + 1);

field_type unit_field_data[MAX_SET_FIELDS + 1] = {
    {UT_UNIT, AT_STR, nullptr, 200, 200, 253},                /* add-name        */
    {UT_UNIT, AT_STR, nullptr, 200, 200, 253},                /* del-name        */
    {UT_UNIT, AT_STR, nullptr, 200, 200, 200},                /* title           */
    {UT_UNIT, AT_DES, nullptr, 200, 200, 253},                /* long-description */
    {UT_UNIT, AT_KEYDES, nullptr, 200, 200, 200},             /* add-extra       */
    {UT_UNIT, AT_STR, nullptr, 200, 200, 200},                /* del-extra       */
    {UT_UNIT, AT_BIT, g_unit_manipulate, 200, 200, 250},      /* manipulate      */
    {UT_UNIT, AT_BIT, g_unit_flags, 200, 200, 250},           /* unit-flags      */
    {UT_UNIT, AT_VAL, nullptr, 200, 200, 230},                /* weight          */
    {UT_UNIT, AT_VAL, nullptr, 200, 200, 230},                /* capacity        */
    {UT_UNIT, AT_VAL, nullptr, 200, 200, 253},                /* max-hp          */
    {UT_UNIT, AT_VAL, nullptr, 200, 200, 253},                /* hp              */
    {UT_UNIT, AT_STR, nullptr, 200, 200, 253},                /* key             */
    {UT_UNIT, AT_VAL, nullptr, 200, 200, 249},                /* alignment       */
    {UT_UNIT, AT_BIT, g_unit_open_flags, 200, 200, 253},      /* open-flags      */
    {UT_UNIT, AT_VAL, nullptr, 200, 200, 253},                /* tgh             */
    {UT_UNIT, AT_VAL, nullptr, 253, 253, 253},                /* lights          */
    {UT_UNIT, AT_VAL, nullptr, 200, 200, 253},                /* bright          */
    {UT_ROOM, AT_BIT, g_room_flags, 200, 200, 200},           /* room-flags      */
    {UT_ROOM, AT_TYP, g_room_landscape, 200, 200, 200},       /* movement        */
    {UT_PC, AT_VAL, nullptr, 255, 255, 255},                  /* cc-info         */
    {UT_ROOM, AT_DIRSTR, nullptr, 200, 200, 200},             /* add-dir-name    */
    {UT_ROOM, AT_DIRSTR, nullptr, 200, 200, 200},             /* del-dir-name    */
    {UT_ROOM, AT_DIRBIT, g_unit_open_flags, 200, 200, 200},   /* dir-flags       */
    {UT_ROOM, AT_DIRSTR, nullptr, 200, 200, 200},             /* dir-key         */
    {UT_OBJ, AT_VAL, nullptr, 200, 240, 200},                 /* value0          */
    {UT_OBJ, AT_VAL, nullptr, 200, 240, 200},                 /* value1          */
    {UT_OBJ, AT_VAL, nullptr, 200, 240, 200},                 /* value2          */
    {UT_OBJ, AT_VAL, nullptr, 200, 240, 200},                 /* value3          */
    {UT_OBJ, AT_VAL, nullptr, 200, 240, 200},                 /* value4          */
    {UT_OBJ, AT_BIT, g_obj_flags, 200, 200, 200},             /* obj-flags       */
    {UT_OBJ, AT_VAL, nullptr, 200, 200, 200},                 /* cost            */
    {UT_OBJ, AT_VAL, nullptr, 200, 200, 200},                 /* rent            */
    {UT_OBJ, AT_TYP, g_obj_types, 200, 240, 200},             /* type            */
    {UT_OBJ, AT_TYP, g_obj_pos, 200, 240, 200},               /* equip           */
    {UT_PC, AT_STR, nullptr, 200, 230, 253},                  /* guild-name      */
    {UT_PC, AT_STR, nullptr, 240, 253, 230},                  /* pwd             */
    {UT_PC, AT_BIT, g_pc_flags, 200, 200, 253},               /* pc-flags        */
    {UT_PC, AT_VAL, nullptr, 200, 200, 253},                  /* crimes          */
    {UT_PC, AT_VAL, nullptr, 200, 200, 200},                  /* drunk           */
    {UT_PC, AT_VAL, nullptr, 200, 200, 200},                  /* full            */
    {UT_PC, AT_VAL, nullptr, 200, 200, 200},                  /* thirsty         */
    {UT_NPC, AT_VAL, nullptr, 200, 200, 253},                 /* default-pos     */
    {UT_NPC, AT_BIT, g_npc_flags, 200, 200, 200},             /* npc-flags       */
    {UT_PC, AT_STR, nullptr, 200, 230, 230},                  /* hometown        */
    {UT_CHAR, AT_VAL, nullptr, 253, 253, 253},                /* exp             */
    {UT_CHAR, AT_BIT, g_char_flags, 200, 200, 253},           /* char-flags      */
    {UT_CHAR, AT_VAL, nullptr, 200, 200, 240},                /* mana            */
    {UT_CHAR, AT_VAL, nullptr, 200, 200, 240},                /* endurance       */
    {UT_CHAR, AT_TYP, g_WpnColl.gettext(), 200, 200, 253},    /* attack-type     */
    {UT_CHAR, AT_VAL, nullptr, 200, 200, 253},                /* hand-quality    */
    {UT_UNIT, AT_VAL, nullptr, 200, 200, 230},                /* height          */
    {UT_CHAR, AT_TYP, g_pc_races, 200, 200, 253},             /* race            */
    {UT_CHAR, AT_TYP, g_char_sex, 200, 200, 253},             /* sex             */
    {UT_NPC, AT_VAL, nullptr, 255, 255, 255},                 /* level           */
    {UT_CHAR, AT_TYP, g_char_pos, 200, 253, 253},             /* position        */
    {UT_CHAR, AT_TYPVAL, g_AbiColl.gettext(), 240, 253, 253}, /* ability         */
    {UT_PC, AT_VAL, nullptr, 230, 253, 253},                  /* skill-points    */
    {UT_PC, AT_VAL, nullptr, 230, 253, 253},                  /* ability-points  */
    {UT_UNIT, AT_VAL, nullptr, 200, 230, 200},                /* remove affects  */
    {UT_PC, AT_STR, nullptr, 200, 200, 230},                  /* add-quest       */
    {UT_PC, AT_STR, nullptr, 200, 200, 230},                  /* del-quest       */
    {UT_CHAR, AT_VAL, nullptr, 200, 253, 230},                /* speed           */
    {UT_PC, AT_STR, nullptr, 255, 254, 254},                  /* add-info        */
    {UT_PC, AT_STR, nullptr, 255, 254, 254},                  /* del-info        */
    {UT_PC, AT_VAL, nullptr, 255, 254, 254},                  /* access          */
    {UT_PC, AT_STR, nullptr, 200, 200, 200},                  /* promptstr       */
    {UT_PC, AT_VAL, nullptr, 230, 230, 230},                  /* age             */
    {UT_PC, AT_VAL, nullptr, 254, 255, 255},                  /* lifespan        */
    {UT_PC, AT_VAL, nullptr, 254, 255, 255},                  /* profession      */
};

// Post-porcessing of adding-extra descriptions.
void edit_extra(descriptor_data *d)
{
    extra_descr_data *exd = nullptr;

    for (exd = d->cgetEditing()->getExtraList().m_pList; exd; exd = exd->next)
    {
        if (exd == d->getEditingReference())
        {
            break; // It still exists!
        }
    }

    if (exd)
    {
        exd->descr = d->getLocalString();
    }
}

// Post-porcessing of adding-extra descriptions.
void edit_info(descriptor_data *d)
{
    extra_descr_data *exd = nullptr;

    for (exd = PC_INFO(d->getEditing()).m_pList; exd; exd = exd->next)
    {
        if (exd == d->getEditingReference())
        {
            break; // It still exists!
        }
    }

    if (exd)
    {
        exd->descr = d->getLocalString();
    }
}

void edit_outside_descr(descriptor_data *d)
{
    d->getEditing()->setDescriptionOfOutside(d->getLocalString());
}

void edit_inside_descr(descriptor_data *d)
{
    d->getEditing()->setDescriptionOfInside(d->getLocalString());
}

int search_block_set(char *arg, const char **list, bool exact)
{
    int i = 0;
    int l = 0;

    if (list == nullptr)
    {
        return -1;
    }

    /* Substitute '_' and get length of string */
    for (l = 0; arg[l]; l++)
    {
        if (arg[l] == '_')
        {
            arg[l] = ' ';
        }
    }

    if (exact)
    {
        for (i = 0; list[i]; i++)
        {
            if (!strcmp(arg, list[i]))
            {
                return i;
            }
        }
    }
    else
    {
        if (l == 0)
        {
            l = 1; /* Avoid "" to match the first available string */
        }

        for (i = 0; list[i]; i++)
        {
            if (!str_nccmp(arg, list[i], l))
            { /* no regard of case */
                return i;
            }
        }
    }

    return -1;
}

// clang-format off
/* show possible fields */
#define GET_FIELD_UT(c)                                               \
    (c) == UT_CHAR   ? "Char"                                         \
    : (c) == UT_PC   ? "Pc"                                           \
    : (c) == UT_NPC  ? "Npc"                                          \
    : (c) == UT_OBJ  ? "Obj"                                          \
    : (c) == UT_UNIT ? "Unit"                                         \
    : (c) == UT_ROOM ? "Room"                                         \
                     : "Not Used"

#define GET_FIELD_AT(c)                                               \
    (c) == AT_VAL      ? "&lt;value&gt;"                              \
    : (c) == AT_BIT    ? "&lt;bitlist&gt;"                            \
    : (c) == AT_TYP    ? "&lt;type&gt;"                               \
    : (c) == AT_STR    ? "&lt;string&gt;"                             \
    : (c) == AT_DES    ? "(enter description)"                        \
    : (c) == AT_UNT    ? "&lt;unitpath&gt;"                           \
    : (c) == AT_KEYDES ? "&lt;keyword&gt; (enter description)"        \
    : (c) == AT_TYPVAL ? "&lt;type&gt; &lt;value&gt;"                 \
    : (c) == AT_DIRBIT ? "&lt;direction&gt; &lt;bitlist&gt;"          \
    : (c) == AT_TYPDES ? "&lt;type&gt; (enter description)"           \
    : (c) == AT_DIRSTR ? "&lt;direction&gt; &lt;string&gt;"           \
    : (c) == AT_DIRUNT ? "&lt;direction&gt; &lt;unitpath&gt;"         \
    : (c) == AT_DIRDES ? "&lt;direction&gt; (enter description)"      \
                       : "Not usable"
// clang-format on

void show_fields(unit_data *ch)
{
    std::string msg{"<table class='colh3'>"};
    for (int i = 0; i < MAX_SET_FIELDS; i++)
    {
        msg += diku::format_to_str("<tr><td>%s :</td><td> on %s. :</td><td>%s</td></tr>",
                                   unit_field_names[i],
                                   GET_FIELD_UT(unit_field_data[i].utype),
                                   GET_FIELD_AT(unit_field_data[i].atype));
    }
    msg += "</table>";
    send_to_char(msg, ch);
}

#undef GET_FIELDT_UT
#undef GET_FIELD_AT

void show_structure(const char *structure[], unit_data *ch)
{
    if (structure == nullptr)
    {
        return;
    }

    std::string s;
    for (char **c = (char **)structure; *c; c++)
    {
        s += diku::format_to_str("%s<br/>", *c);
    }

    if (s.empty())
    {
        send_to_char("Not defined yet.<br/>", ch);
    }
    else
    {
        send_to_char(s, ch);
    }
}

long int get_bit(char *bitlst, const char *structure[])
{
    char *l = nullptr;
    char *s = nullptr;
    char bit[MAX_STRING_LENGTH];
    long int bitval = 0;
    long int tmpval = 0;

    /* walk through bitlist */
    for (l = bitlst; *l;)
    {
        /* copy bitname */
        for (s = bit; *l && *l != '|'; *(s++) = *(l++))
        {
            ;
        }

        *s = 0;
        if (*l)
        {
            l++;
        }

        if (!str_is_empty(bit))
        {
            if ((tmpval = search_block_set(bit, structure, FALSE)) == -1)
            {
                return -1;
            }
            SET_BIT(bitval, 1 << tmpval);
        }
    }
    return bitval;
}

int get_type(char *typdef, const char *structure[])
{
    return search_block_set(typdef, structure, FALSE);
}

/* modification of anything in units */
void do_set(unit_data *ch, char *argument, const command_info *cmd)
{
    char arg[MAX_STRING_LENGTH];
    int type = 0;

    char strarg[MAX_STRING_LENGTH];
    int typarg = 0;
    long int valarg = 0;
    long int bitarg = 0;

    file_index_type *untarg = nullptr;
    extra_descr_data *ed = nullptr;
    unit_data *unt = nullptr;
    unit_affected_type *aff = nullptr;

    if (!CHAR_DESCRIPTOR(ch))
    {
        return;
    }

    /* Check argument */
    if (str_is_empty(argument))
    {
        send_to_char("Syntax: set &lt;name&gt; &lt;field&gt; &lt;arguments&gt;.<br/>", ch);
        return;
    }

    str_next_word(argument, arg);

    if (str_ccmp(arg, "room") == 0)
    {
        argument = str_next_word(argument, arg);
        unt = ch->getUnitIn();
    }
    else
    {
        unt = find_unit(ch, &argument, nullptr, FIND_UNIT_WORLD | FIND_UNIT_SURRO | FIND_UNIT_INVEN | FIND_UNIT_EQUIP);
        if (unt == nullptr)
        {
            send_to_char("No such thing.<br/>", ch);
            return;
        }
        /* Temporary fix, as modifying money CAN crash the game!   /gnort */
        else if (IS_MONEY(unt) && CHAR_LEVEL(ch) < 255)
        {
            send_to_char("You can't modify money yet, sorry.<br/>", ch);
            return;
        }
    }

    /* find field to change */
    argument = str_next_word(argument, arg);

    /* Partial match on fields */
    if ((type = search_block(arg, unit_field_names, FALSE)) == -1)
    {
        send_to_char("Invalid field.<br/>", ch);
        show_fields(ch);
        return;
    }

    /* check if level of users is ok */
    if (CHAR_LEVEL(ch) < unit_field_data[type].minplayer && unt != ch && unt->isPC())
    {
        send_to_char("Authority to set field for OTHER PLAYERS denied!<br/>", ch);
        return;
    }

    /* check if level of users is ok */
    if ((CHAR_LEVEL(ch) < unit_field_data[type].minother && unt != ch && unt->isPC()) ||
        (CHAR_LEVEL(ch) < unit_field_data[type].minself && unt == ch))
    {
        send_to_char("Authority to set field denied!<br/>", ch);
        return;
    }

    /* see if field is valid for unit */
    if ((unit_field_data[type].utype == UT_ROOM && !unt->isRoom()) || (unit_field_data[type].utype == UT_OBJ && !unt->isObj()) ||
        (unit_field_data[type].utype == UT_NPC && !unt->isNPC()) || (unit_field_data[type].utype == UT_PC && !unt->isPC()) ||
        (unit_field_data[type].utype == UT_CHAR && !unt->isChar()))
    {
        send_to_char("Field invalid for type of unit.<br/>", ch);
        return;
    }

    argument = (char *) skip_spaces(argument);

    /* read in field parameters */
    switch (unit_field_data[type].atype)
    {
        case AT_STR:
            if (str_is_empty(argument))
            {
                send_to_char("Argument expected.<br/>", ch);
                return;
            }
            break;

        case AT_DES:
            if (unit_is_edited(unt))
            {
                send_to_char("Unit is already being edited.<br/>", ch);
                return;
            }
            CHAR_DESCRIPTOR(ch)->setEditing(unt);
            /* handle this later */
            break;

        case AT_VAL:
        {
            send_to_char("Arg:&lt;value&gt;<br/>", ch);
            argument = str_next_word(argument, arg);
            if (str_is_empty(arg))
            {
                send_to_char("Numeric argument expected.<br/>", ch);
                return;
            }
            valarg = atoi(arg);
            auto msg = diku::format_to_str("Value is %ld<br/>", valarg);
            send_to_char(msg, ch);
        }
        break;

        case AT_BIT:
        {
            send_to_char("Arg:&lt;bitlist&gt;<br/>", ch);
            argument = str_next_word(argument, arg);
            if ((bitarg = get_bit(arg, unit_field_data[type].structure)) == -1)
            {
                send_to_char("Invalid or missing bit for field<br/>", ch);
                show_structure(unit_field_data[type].structure, ch);
                return;
            }
            auto msg = diku::format_to_str("Bit found is %ld<br/>", bitarg);
            send_to_char(msg, ch);
        }
        break;

        case AT_TYP:
        {
            send_to_char("Arg:&lt;type&gt;<br/>", ch);
            argument = str_next_word(argument, arg);
            if ((typarg = get_type(arg, unit_field_data[type].structure)) == -1)
            {
                send_to_char("Invalid or missing type for field.<br/>", ch);
                show_structure(unit_field_data[type].structure, ch);
                return;
            }
            auto msg = diku::format_to_str("Type found is %s [%d]<br/>", unit_field_data[type].structure[typarg], typarg);
            send_to_char(msg, ch);
        }
        break;

        case AT_UNT:
        {
            send_to_char("Arg:&lt;unitpath&gt;<br/>", ch);
            argument = str_next_word(argument, strarg);
            /* find unit by 'path' name in arg */
            if ((untarg = str_to_file_index(strarg)) == nullptr)
            {
                send_to_char("Invalid or missing unit path for field.<br/>", ch);
                return;
            }
            auto msg = diku::format_to_str("Unit pointer is [%s@%s]<br/>", untarg->getName(), untarg->getZone()->getName());
            send_to_char(msg, ch);
        }
        break;

        case AT_KEYDES:
            send_to_char("Arg:&lt;string&gt; (description)<br/>", ch);
            if (str_is_empty(argument))
            {
                send_to_char("Missing string argument.<br/>", ch);
                return;
            }
            if (unit_is_edited(unt))
            {
                send_to_char("Unit is already being edited.<br/>", ch);
                return;
            }
            CHAR_DESCRIPTOR(ch)->setEditing(unt);
            /* handle takes care of the rest */
            break;

        case AT_TYPVAL:
        {
            send_to_char("Arg:&lt;type&gt; &lt;value&gt;<br/>", ch);
            argument = str_next_word(argument, arg);
            if ((typarg = get_type(arg, unit_field_data[type].structure)) == -1)
            {
                send_to_char("Invalid or missing type for field.<br/>", ch);
                show_structure(unit_field_data[type].structure, ch);
                return;
            }
            auto msg = diku::format_to_str("Type found is %s [%d]<br/>", unit_field_data[type].structure[typarg], typarg);
            send_to_char(msg, ch);
            argument = str_next_word(argument, arg);
            if (str_is_empty(arg))
            {
                send_to_char("Numeric argument expected.<br/>", ch);
                return;
            }
            valarg = atoi(arg);
            msg = diku::format_to_str("Value is %ld<br/>", valarg);
            send_to_char(msg, ch);
        }
        break;

        case AT_DIRBIT:
        {
            send_to_char("Arg:&lt;direction&gt; <bitlist><br/>", ch);
            argument = str_next_word(argument, arg);
            if ((typarg = get_type(arg, g_dirs)) == -1)
            {
                send_to_char("Invalid direction.<br/>", ch);
                show_structure(g_dirs, ch);
                return;
            }
            auto msg = diku::format_to_str("Direction found is %s [%d]<br/>", g_dirs[typarg], typarg);
            send_to_char(msg, ch);
            argument = str_next_word(argument, arg);
            if ((bitarg = get_bit(arg, unit_field_data[type].structure)) == -1)
            {
                send_to_char("Invalid or missing bit for field.<br/>", ch);
                show_structure(unit_field_data[type].structure, ch);
                return;
            }
            msg = diku::format_to_str("Bit found is %ld<br/>", bitarg);
            send_to_char(msg, ch);
        }
        break;

        case AT_DIRSTR:
        {
            send_to_char("Arg:&lt;direction&gt; <string><br/>", ch);
            argument = str_next_word(argument, arg);
            if ((typarg = get_type(arg, g_dirs)) == -1)
            {
                send_to_char("Invalid direction.<br/>", ch);
                show_structure(g_dirs, ch);
                return;
            }
            auto msg = diku::format_to_str("Direction found is %s [%d]<br/>", g_dirs[typarg], typarg);
            send_to_char(msg, ch);
            if (str_is_empty(argument))
            {
                send_to_char("Missing string argument.<br/>", ch);
                return;
            }
            argument = (char *) skip_spaces(argument);
        }
        break;

        case AT_DIRUNT:
        {
            send_to_char("Arg:&lt;direction&gt; <unitpath><br/>", ch);
            argument = str_next_word(argument, arg);
            if ((typarg = get_type(arg, g_dirs)) == -1)
            {
                send_to_char("Invalid or missing direction.<br/>", ch);
                show_structure(g_dirs, ch);
                return;
            }
            auto msg = diku::format_to_str("Direction found is %s [%d]<br/>", g_dirs[typarg], typarg);
            send_to_char(msg, ch);
            argument = str_next_word(argument, arg);
            if ((untarg = str_to_file_index(arg)) == nullptr)
            {
                send_to_char("Invalid or missing unit path for field.<br/>", ch);
                return;
            }
            msg = diku::format_to_str("Unit pointer is [%s@%s]<br/>", untarg->getName(), untarg->getZone()->getName());
            send_to_char(msg, ch);
        }
        break;

        case AT_DIRDES:
        {
            send_to_char("Arg:&lt;direction&gt; (description)<br/>", ch);
            argument = str_next_word(argument, arg);
            if ((typarg = get_type(arg, g_dirs)) == -1)
            {
                send_to_char("Invalid or missing direction.<br/>", ch);
                show_structure(g_dirs, ch);
                return;
            }
            auto msg = diku::format_to_str("Direction found is %s [%d]<br/>", g_dirs[typarg], typarg);
            send_to_char(msg, ch);
            if (unit_is_edited(unt))
            {
                send_to_char("Unit is already being edited.<br/>", ch);
                return;
            }
            CHAR_DESCRIPTOR(ch)->setEditing(unt);
            /* handle rest later */
        }
        break;

        default:
            send_to_char("Forbidden argument type for field, please "
                         "contact implementators.<br/>",
                         ch);
            return;
    }

    argument = (char *) skip_spaces(argument);
    strip_trailing_spaces(argument);

    /* insert data read in argument */
    switch (type)
    {
        case 0: /* "add-name" */
            if (unt->isPC() && CHAR_LEVEL(ch) < 255)
            {
                send_to_char("Not allowed to modify PC's.<br/>", ch);
                return;
            }
            argument = (char *) skip_spaces(argument);
            strip_trailing_blanks(argument);
            str_remspc(argument);

            unt->getNames().AppendName(argument);
            send_to_char("The extra name was added.<br/>", ch);
            return;

        case 1: /* "del-name" */
            if (!unt->getNames().Name(0) || !unt->getNames().Name(1))
            {
                send_to_char("Must have minimum of two names<br/>", ch);
                return;
            }
            argument = (char *) skip_spaces(argument);
            unt->getNames().RemoveName(argument);
            send_to_char("Name may have been deleted.<br/>", ch);
            return;

        case 2: /* "title",   */
            unt->setTitle(argument);

            send_to_char("Title modified.<br/>", ch);
            return;

        case 3: /* "outside description" */
            send_to_char("Modifying long description.<br/>", ch);
            CHAR_DESCRIPTOR(ch)->setPostEditFunctionPtr(edit_outside_descr);
            set_descriptor_fptr(CHAR_DESCRIPTOR(ch), interpreter_string_add, TRUE);
            return;

        case 4: /* "add-extra" */
            argument = str_next_word(argument, strarg);
            act("Searching for $2t.", A_ALWAYS, ch, strarg, cActParameter(), TO_CHAR);

            if ((ed = unit_find_extra(strarg, unt)) == nullptr)
            {
                /* the field was not found. create a new one. */
                ed = new (extra_descr_data);
                while (*strarg) /* insert names */
                {
                    ed->names.AppendName(strarg);
                    argument = str_next_word(argument, strarg);
                }
                unt->getExtraList().add(ed);

                send_to_char("New field.<br/>", ch);
            }
            else
            {
                /* add the rest of the names if they do not exist */
                argument = str_next_word(argument, strarg);
                while (*strarg)
                {
                    if (ed->names.IsName(strarg))
                    {
                        ed->names.AppendName(strarg);
                    }

                    argument = str_next_word(argument, strarg);
                }

                send_to_char("Modifying existing description.<br/>", ch);
            }

            CHAR_DESCRIPTOR(ch)->setEditReference(ed);
            CHAR_DESCRIPTOR(ch)->setPostEditFunctionPtr(edit_extra);
            CHAR_DESCRIPTOR(ch)->setEditing(unt);
            set_descriptor_fptr(CHAR_DESCRIPTOR(ch), interpreter_string_add, TRUE);
            return;

        case 5: /* "del-extra" */
            if (str_is_empty(argument))
            {
                send_to_char("You must supply a field name.<br/>", ch);
                return;
            }

            unt->getExtraList().remove(argument);
            send_to_char("Trying to delete field.<br/>", ch);
            return;

        case 6: /* "manipulate-flags" */
            unt->setAllManipulateFlags(bitarg);
            return;

        case 7: /* "unit-flags" */
            if (IS_SET(unt->getUnitFlags(), UNIT_FL_TRANS) && !IS_SET(bitarg, UNIT_FL_TRANS))
            {
                trans_unset(unt);
            }
            else if (!IS_SET(unt->getUnitFlags(), UNIT_FL_TRANS) && IS_SET(bitarg, UNIT_FL_TRANS))
            {
                trans_set(unt);
            }

            unt->setAllUnitFlags(bitarg);
            return;

        case 8: /* "weight" */
        {
            if (unt->getUnitContains())
            {
                send_to_char("The unit isn't empty. Setting weight is supposed to happen on empty units only. Setting anyway<br/>", ch);
            }

            int dif = valarg - unt->getBaseWeight();

            /* set new baseweight */
            unt->setBaseWeight(valarg);

            /* update weight */
            weight_change_unit(unt, dif);

            // Now make weight and base weight equal
            dif = unt->getBaseWeight() - unt->getWeight();
            weight_change_unit(unt, dif);
            // UNIT_BASE_WEIGHT(unt) = UNIT_WEIGHT(unt) = valarg;
            return;
        }

        case 9: /* "capacity" */
            unt->setCapacity(valarg);
            return;

        case 10: /* "max_hp" */
            unt->setMaximumHitpoints(valarg);
            return;

        case 11: /* "hp" */
            unt->setCurrentHitpoints(valarg);
            return;

        case 12: /* "key" */
            argument = str_next_word(argument, strarg);
            unt->setKey(str_dup(strarg));
            return;

        case 13: /* "alignment" */
            if (!unt->setAlignment(valarg))
            {
                auto msg =
                    diku::format_to_str("Shame on you: Value must be in %+d..%+d!<br/>", unit_data::MinAlignment, unit_data::MaxAlignment);
                send_to_char(msg, ch);
            }
            return;

        case 14: /* "open_flags" */
            unt->setAllOpenFlags(bitarg);
            return;

        case 15: /* "tgh" OBSOLETE */
            /* UNIT_TGH(unt) = valarg; */
            return;

        case 16: /* "lights" */
            unt->setNumberOfActiveLightSources(valarg);
            unt->setLightOutput(valarg);
            unt->setTransparentLightOutput(valarg);
            send_to_char("WARNING: This value is absolute and will cause 'darkness' "
                         "bugs if not used properly! Only use this to fix darkness "
                         "- use bright for changing the illumination!<br/>",
                         ch);
            return;

        case 17: /* "bright" */
            if (!is_in(valarg, -6, 6))
            {
                send_to_char("Expected -6..+6<br/>", ch);
                return;
            }
            modify_bright(unt, valarg - unt->getLightOutput());
            return;

        case 18: /* "room_flags" */
            UROOM(unt)->setAllRoomFlags(bitarg);
            return;

        case 19: /* "movement" */
            UROOM(unt)->setLandscapeTerrain(typarg);
            return;

        case 20: /* "ccinfo" */
            if (valarg == -1)
            {
                send_to_char("Erasing CC information.<br/>", ch);
                PC_ACCOUNT(unt).setLastFourDigitsofCreditCard(-1);
                PC_ACCOUNT(unt).setCrackAttempts(0);
            }
            else
            {
                if (is_in(valarg, 0, 9999))
                {
                    send_to_char("Setting CC information.<br/>", ch);
                    PC_ACCOUNT(unt).setLastFourDigitsofCreditCard(valarg);
                    PC_ACCOUNT(unt).setCrackAttempts(0);
                }
                else
                {
                    send_to_char("Illegal value, expected -1 or 0..9999.<br/>", ch);
                }
            }
            return;

        case 21: /* "add-dir-name" */
            if (!ROOM_EXIT(unt, typarg))
            {
                send_to_char("No such exit.<br/>", ch);
                return;
            }

            argument = str_next_word(argument, strarg);
            ROOM_EXIT(unt, typarg)->getOpenName().AppendName(strarg);
            return;

        case 22: /* "del-dir-name" */
            if (!ROOM_EXIT(unt, typarg))
            {
                send_to_char("No such exit.<br/>", ch);
                return;
            }

            argument = str_next_word(argument, strarg);
            ROOM_EXIT(unt, typarg)->getOpenName().AppendName(strarg);
            return;

        case 23: /* "dir-flags" */
            if (!ROOM_EXIT(unt, typarg))
            {
                send_to_char("No such exit.<br/>", ch);
                return;
            }

            ROOM_EXIT(unt, typarg)->setDoorFlags(bitarg);
            return;

        case 24: /* "dir-key" */
            if (!ROOM_EXIT(unt, typarg))
            {
                send_to_char("No such exit.<br/>", ch);
                return;
            }

            argument = str_next_word(argument, strarg);
            ROOM_EXIT(unt, typarg)->setKey(str_dup(strarg));
            return;

        case 25: /* "value0" */
            switch (OBJ_TYPE(unt))
            {
                case ITEM_MONEY:
                    send_to_char("Operation not allowed on this type of object.<br/>", ch);
                    return;
            }

            /* Should be expanded to handle the different object-types */
            UOBJ(unt)->setValueAtIndexTo(0, valarg);
            return;

        case 26: /* "value1" */
            switch (OBJ_TYPE(unt))
            {
                case ITEM_MONEY:
                    send_to_char("Operation not allowed on this type of object.<br/>", ch);
                    return;
            }
            /* Should be expanded to handle the different object-types */
            UOBJ(unt)->setValueAtIndexTo(1, valarg);
            return;

        case 27: /* "value2" */
            switch (OBJ_TYPE(unt))
            {
                case ITEM_MONEY:
                    send_to_char("Operation not allowed on this type of object.<br/>", ch);
                    return;
            }
            /* Should be expanded to handle the different object-types */
            UOBJ(unt)->setValueAtIndexTo(2, valarg);
            return;

        case 28: /* "value3" */
            switch (OBJ_TYPE(unt))
            {
                case ITEM_MONEY:
                    send_to_char("Operation not allowed on this type of object.<br/>", ch);
                    return;
            }
            /* Should be expanded to handle the different object-types */
            UOBJ(unt)->setValueAtIndexTo(3, valarg);
            return;

        case 29: /* "value4" */
            switch (OBJ_TYPE(unt))
            {
                case ITEM_MONEY:
                    send_to_char("Operation not allowed on this type of object.<br/>", ch);
                    return;
            }
            /* Should be expanded to handle the different object-types */
            UOBJ(unt)->setValueAtIndexTo(4, valarg);
            return;

        case 30: /* "obj-flags" */
            UOBJ(unt)->setAllObjectFlags(bitarg);
            return;

        case 31: /* "cost" */
            switch (OBJ_TYPE(unt))
            {
                case ITEM_MONEY:
                    send_to_char("Operation not allowed on this type of object.<br/>", ch);
                    return;
            }

            UOBJ(unt)->setPriceInGP(valarg);
            return;

        case 32: /* "rent" */
            UOBJ(unt)->setPricePerDay(valarg);
            return;

        case 33: /* "type" */
            switch (OBJ_TYPE(unt))
            {
                case ITEM_MONEY:
                    send_to_char("Operation not allowed on this type of object.<br/>", ch);
                    return;
            }

            if (typarg == ITEM_MONEY)
            {
                send_to_char("Operation not allowed to that kind of object.<br/>", ch);
                return;
            }

            UOBJ(unt)->setObjectItemType(typarg);
            return;

        case 34: /* "equip" */
            UOBJ(unt)->setEquipmentPosition(typarg);
            return;

        case 35: /* "guild-name" */
            if (str_ccmp_next_word(argument, "none"))
            {
                UPC(unt)->freeGuild();
                send_to_char("Changed.<br/>", ch);
                return;
            }

            UPC(unt)->setGuild(argument);
            send_to_char("Changed.<br/>", ch);
            return;

        case 36: /* "pwd" */
            argument = str_next_word(argument, strarg);
            if (CHAR_LEVEL(ch) < CHAR_LEVEL(unt))
            {
                slog(LOG_ALL, 0, "WARNING: %s attempted to set %s password", ch->getNames().Name(), unt->getNames().Name());
                send_to_char("You can not change a password of a higher level immortal", ch);
            }
            else
            {
                slog(LOG_ALL, 0, "PASSWORD: %s changed %s's password.", ch->getNames().Name(), unt->getNames().Name());
                UPC(unt)->setPassword(crypt(strarg, PC_FILENAME(unt)));
                send_to_char("The password has been set.<br/>", ch);
            }
            return;

        case 37: /* "setup-flags" */
            UPC(unt)->setAllPCFlags(bitarg);
            return;

        case 38: /* "crimes" */
            UPC(unt)->setNumberOfCrimesCommitted(valarg);
            return;

        case 39: /* "drunk" */
            UPC(unt)->setConditionAtIndexTo(DRUNK, valarg);
            return;

        case 40: /* "full" */
            UPC(unt)->setConditionAtIndexTo(FULL, valarg);
            return;

        case 41: /* "thirsty" */
            UPC(unt)->setConditionAtIndexTo(THIRST, valarg);
            return;

        case 42: /* "default-pos" */
            UNPC(unt)->setDefaultPosition(valarg);
            return;

        case 43: /* "act-flags" */
            UNPC(unt)->setAllNPCFlags(bitarg);
            return;

        case 44: /* hometown */
            UPC(unt)->setHometown(argument);
            send_to_char("Changed.<br/>", ch);
            return;

        case 45: /* "exp" */
            if (CHAR_LEVEL(unt) < MORTAL_MAX_LEVEL && valarg > required_xp(1000))
            {
                send_to_char("You are not allowed to set exp that high for "
                             "mortals<br/>",
                             ch);
            }
            else
            {
                UCHAR(unt)->setPlayerExperience(valarg);
            }
            return;

        case 46: /* "affected-by" */
            UCHAR(unt)->setAllCharacterFlags(bitarg);
            return;

        case 47: /* "mana" */
            UCHAR(unt)->setMana(valarg);
            return;

        case 48: /* "endurance" */
            UCHAR(unt)->setEndurance(valarg);
            return;

        case 49: /* "attack-type" */
            UCHAR(unt)->setAttackType(typarg);
            return;

        case 50: /* "hand-quality" OBSOLETE */
            /*
               if (is_in(valarg, 0, 200))
               CHAR_HAND_QUALITY(unt) = valarg;
               else
               send_to_char("Shame on you: Value must be in 0%..200%!<br/>", ch); */
            return;

        case 51: /* "height" */
            unt->setSize(valarg);
            return;

        case 52: /* "race" */
            UCHAR(unt)->setRace(typarg);
            return;

        case 53: /* "sex" */
            UCHAR(unt)->setSex(typarg);
            return;

        case 54: /* "level" */
            if (is_in(valarg, 0, 199))
            {
                UCHAR(unt)->setLevel(valarg);
            }
            else
            {
                send_to_char("Shame on you: Value must be in 0..199!<br/>", ch);
            }
            return;

        case 55: /* "position" */
            UCHAR(unt)->setPosition(typarg);
            return;

        case 56: /* "ability" */
            if (is_in(valarg, 0, 250))
            {
                UCHAR(unt)->setAbilityAtIndexTo(typarg, valarg);
            }
            else
            {
                send_to_char("Shame on you: Value must be in 0%..250%!<br/>", ch);
            }
            return;

        case 57: /* "skill-points" */
            UPC(unt)->setSkillPoints(valarg);
            return;

        case 58: /* "ability-points" */
            UPC(unt)->setAbilityPoints(valarg);
            return;

        case 59: /* "remove-affect" */
            if ((aff = affected_by_spell(unt, valarg)))
            {
                destroy_affect(aff);
                send_to_char("Affect attempted removed.<br/>", ch);
            }
            else
            {
                send_to_char("No such affect.<br/>", ch);
            }
            return;

        case 60: /* "add-quest" */
            quest_add(unt, argument, "");
            send_to_char("New quest.<br/>", ch);
            return;

        case 61: /* "del-quest" */
            if (str_is_empty(argument))
            {
                send_to_char("You must supply a field name.<br/>", ch);
                return;
            }

            send_to_char("Attempting to remove such a quest.<br/>", ch);

            PC_QUEST(unt).remove(argument);
            return;

        case 62: /* "speed" */
            if (is_in(valarg, SPEED_MIN, SPEED_MAX))
            {
                UCHAR(unt)->setSpeed(valarg);
            }
            else
            {
                send_to_char("Speed must be in [12..200]!<br/>", ch);
            }
            return;

        case 63: /* "add-info" */
            argument = str_next_word(argument, strarg);
            act("Searching for $2t.", A_ALWAYS, ch, strarg, cActParameter(), TO_CHAR);

            if ((ed = PC_INFO(unt).find_raw(strarg)) == nullptr)
            {
                /* the field was not found. create a new one. */
                ed = new (extra_descr_data);
                while (*strarg) /* insert names */
                {
                    ed->names.AppendName(strarg);
                    argument = str_next_word(argument, strarg);
                }
                PC_INFO(unt).add(ed);

                send_to_char("New field.<br/>", ch);
            }
            else
            {
                /* add the rest of the names if they do not exist */
                argument = str_next_word(argument, strarg);
                while (*strarg)
                {
                    if (ed->names.IsName(strarg))
                    {
                        ed->names.AppendName(strarg);
                    }

                    argument = str_next_word(argument, strarg);
                }

                send_to_char("Modifying existing description.<br/>", ch);
            }

            CHAR_DESCRIPTOR(ch)->setEditReference(ed);
            CHAR_DESCRIPTOR(ch)->setPostEditFunctionPtr(edit_info);
            CHAR_DESCRIPTOR(ch)->setEditing(unt);
            set_descriptor_fptr(CHAR_DESCRIPTOR(ch), interpreter_string_add, TRUE);
            return;

        case 64: /* "del-info" */
            if (str_is_empty(argument))
            {
                send_to_char("You must supply a field name.<br/>", ch);
                return;
            }

            send_to_char("Attempting to remove such an info.<br/>", ch);

            PC_INFO(unt).remove(argument);
            return;

        case 65: /* "access" */
            UPC(unt)->setAccessLevel(valarg);
            return;

        case 66: /* "promptstr" */
            if (UPC(unt)->getPromptString() != nullptr)
            {
                UPC(unt)->freePromptString();
            }
            UPC(unt)->setPromptString(str_dup(argument));
            send_to_char("Prompt String Modified.<br/>", ch);
            return;

        case 67: /* "age" */
            /* This sets a time_t so that the PC's age
             * will be as many years as valarg
             */
            if (is_in(valarg, 10, 1000))
            {
                slog(LOG_BRIEF,
                     CHAR_LEVEL(ch),
                     "SET %s set %s's age from %.2f to %d",
                     ch->getNames().Name(),
                     unt->getNames().Name(),
                     PC_TIME(unt).getPlayerBirthday() / (1.0 * SECS_PER_MUD_YEAR),
                     valarg);

                PC_TIME(unt).setPlayerBirthday(time(nullptr) - (valarg * SECS_PER_MUD_YEAR));
            }
            else
            {
                send_to_char("Value must be in 10..1000!<br/>", ch);
            }
            return;

        case 68: /* "lifespan" */
            /* This sets a time_t so that the PC's age
             * will be as many years as valarg
             */
            if (is_in(valarg, 10, 1000))
            {
                slog(LOG_BRIEF,
                     CHAR_LEVEL(ch),
                     "SET %s set %s's lifespan from %d to %d",
                     ch->getNames().Name(),
                     unt->getNames().Name(),
                     PC_LIFESPAN(unt),
                     valarg);
                UPC(unt)->setLifespan(valarg);
            }
            else
            {
                send_to_char("Value must be in 10..1000!<br/>", ch);
            }
            return;

        case 69: /* "profession" */
            if (is_in(valarg, 0, PROFESSION_MAX - 1))
            {
                slog(LOG_BRIEF,
                     CHAR_LEVEL(ch),
                     "SET %s set %s's profession from %d to %d",
                     ch->getNames().Name(),
                     unt->getNames().Name(),
                     PC_PROFESSION(unt),
                     valarg);
                UPC(unt)->setProfession(valarg);
            }
            else
            {
                send_to_char("Value must be in 0..PROFESION MAX<br/>", ch);
            }
            return;

        default: /* undefined field type */
            send_to_char("Field-type is undefined, please contact implementor<br/>", ch);
            return;
    }
}

/*
 *  Modification of character skills
 */

#define SET_SKILL 0
#define SET_SPELL 1
#define SET_WEAPON 2

static const char *skill_field_names[] = {"skill", "spell", "weapon", nullptr};

void do_setskill(unit_data *ch, char *argument, const command_info *cmd)
{
    int type = 0;
    int skillarg = 0;
    int valarg = 0;
    char arg[MAX_STRING_LENGTH];
    unit_data *unt = nullptr;

    if (!CHAR_DESCRIPTOR(ch))
    {
        return;
    }

    /* Check argument */
    if (str_is_empty(argument))
    {
        send_to_char("Syntax: setskill &lt;name&gt; (skill|spell|weapon)"
                     " &lt;field&gt; &lt;value&gt;.<br/>",
                     ch);
        return;
    }

    /* find unit to set */
    if ((unt = find_unit(ch, &argument, nullptr, FIND_UNIT_WORLD)) == nullptr)
    {
        send_to_char("No such person or thing<br/>", ch);
        return;
    }

    if (!unt->isChar())
    {
        send_to_char("Unit-type must be char<br/>", ch);
        return;
    }

    /* find field to change */
    argument = str_next_word(argument, arg);
    /* Partial match on fields */
    if ((type = search_block(arg, skill_field_names, TRUE)) == -1)
    {
        send_to_char("Missing or invalid skill-type, <br/>"
                     "Use 'skill' 'spell' or 'weapon'.<br/>",
                     ch);
        return;
    }

    argument = str_next_word(argument, arg);

    switch (type)
    {
        case SET_SKILL:
            if (!unt->isPC())
            {
                send_to_char("Skills are only for PC's<br/>", ch);
                return;
            }
            if ((skillarg = get_type(arg, g_SkiColl.text)) == -1)
            {
                send_to_char("Invalid or missing skill<br/>", ch);
                show_structure(g_SkiColl.text, ch);
                return;
            }
            argument = str_next_word(argument, arg);
            valarg = atoi(arg);
            UPC(unt)->setSkillAtIndexTo(skillarg, valarg);
            break;

        case SET_SPELL:
            if ((skillarg = get_type(arg, g_SplColl.text)) == -1)
            {
                send_to_char("Invalid or missing spell<br/>", ch);
                show_structure(g_SplColl.text, ch);
                return;
            }
            if (skillarg > SPL_EXTERNAL && unt->isNPC())
            {
                send_to_char("Only spell-groups for NPC<br/>", ch);
                return;
            }
            argument = str_next_word(argument, arg);
            valarg = atoi(arg);

            if (unt->isPC())
            {
                UPC(unt)->setSpellSKillAtIndexTo(skillarg, valarg);
            }
            else
            {
                UNPC(unt)->setSpellSkillAtIndexTo(skillarg, valarg);
            }
            break;

        case SET_WEAPON:
            if ((skillarg = get_type(arg, g_WpnColl.text)) == -1)
            {
                send_to_char("Invalid or missing weaponskill<br/>", ch);
                show_structure(g_WpnColl.text, ch);
                return;
            }
            if (skillarg > WPN_SPECIAL && unt->isNPC())
            {
                send_to_char("Only weapon-groups for NPC<br/>", ch);
                return;
            }
            argument = str_next_word(argument, arg);
            valarg = atoi(arg);

            if (unt->isPC())
            {
                UPC(unt)->setWeaponSkillAtIndexTo(skillarg, valarg);
            }
            else
            {
                UNPC(unt)->setWeaponSkillAtIndexTo(skillarg, valarg);
            }
    }

    auto msg = diku::format_to_str("New value: %d<br/>Ok.<br/>", valarg);
    send_to_char(msg, ch);
}
