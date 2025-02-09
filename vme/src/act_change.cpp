/*
 $Author: tperry $
 $RCSfile: act_change.cpp,v $
 $Date: 2001/04/29 03:46:05 $
 $Revision: 2.1 $
 */

#include "comm.h"
#include "dbfind.h"
#include "dilrun.h"
#include "formatter.h"
#include "interpreter.h"
#include "system.h"
#include "textutil.h"
#include "utils.h"

#include <cstdlib>

static void chg_wimpy(unit_data *ch)
{
    if (IS_SET(CHAR_FLAGS(ch), CHAR_WIMPY))
    {
        send_to_char("You feel brave again.<br/>", ch);
    }
    else
    {
        send_to_char("Ok, you'll flee when death is near.<br/>", ch);
    }

    UCHAR(ch)->toggleCharacterFlag(CHAR_WIMPY);
}

static void chg_expert(unit_data *ch)
{
    if (IS_SET(PC_FLAGS(ch), PC_EXPERT))
    {
        send_to_char("You are now in normal mode.<br/>", ch);
    }
    else
    {
        send_to_char("You are now in expert mode.<br/>", ch);
    }

    UPC(ch)->togglePCFlag(PC_EXPERT);
}

static void chg_brief(unit_data *ch)
{
    if (IS_SET(PC_FLAGS(ch), PC_BRIEF))
    {
        send_to_char("Brief mode off.<br/>", ch);
    }
    else
    {
        send_to_char("Brief mode on.<br/>", ch);
    }

    UPC(ch)->togglePCFlag(PC_BRIEF);
}

static void chg_compact(unit_data *ch)
{
    if (IS_SET(PC_FLAGS(ch), PC_COMPACT))
    {
        send_to_char("You are now in the uncompacted mode.<br/>", ch);
    }
    else
    {
        send_to_char("You are now in compact mode.<br/>", ch);
    }

    UPC(ch)->togglePCFlag(PC_COMPACT);
}

static void chg_peaceful(unit_data *ch)
{
    if (IS_SET(CHAR_FLAGS(ch), CHAR_PEACEFUL))
    {
        send_to_char("They will come in peace and leave in pieces.<br/>", ch);
    }
    else
    {
        send_to_char("You will no longer attack aggressors.<br/>", ch);
    }

    UCHAR(ch)->toggleCharacterFlag(CHAR_PEACEFUL);
}

static void chg_prompt(unit_data *ch)
{
    UPC(ch)->togglePCFlag(PC_PROMPT);
    send_to_char("Prompt changed.<br/>", ch);
}

static void chg_inform(unit_data *ch)
{
    UPC(ch)->togglePCFlag(PC_INFORM);

    if (IS_SET(PC_FLAGS(ch), PC_INFORM))
    {
        send_to_char("You will now get more information.<br/>", ch);
    }
    else
    {
        send_to_char("You will now get less information.<br/>", ch);
    }
}

static void chg_shout(unit_data *ch)
{
    if (IS_SET(PC_FLAGS(ch), PC_NOSHOUT))
    {
        send_to_char("You can now hear shouts again.<br/>", ch);
    }
    else
    {
        send_to_char("From now on, you won't hear shouts.<br/>", ch);
    }

    UPC(ch)->togglePCFlag(PC_NOSHOUT);
}

static void chg_tell(unit_data *ch)
{
    if (IS_SET(PC_FLAGS(ch), PC_NOTELL))
    {
        send_to_char("You can now hear tells again.<br/>", ch);
    }
    else
    {
        send_to_char("From now on, you won't hear tells.<br/>", ch);
    }

    UPC(ch)->togglePCFlag(PC_NOTELL);
}

static void chg_exits(unit_data *ch)
{
    if (IS_SET(PC_FLAGS(ch), PC_EXITS))
    {
        send_to_char("Exit information disabled.<br/>", ch);
    }
    else
    {
        send_to_char("Exit information enabled.<br/>", ch);
    }

    UPC(ch)->togglePCFlag(PC_EXITS);
}

static void chg_columns(unit_data *ch, const char *arg)
{
    if (str_is_empty(arg) || !str_is_number(arg))
    {
        send_to_char("You must enter a column number between 40 and 160.<br/>", ch);
        return;
    }

    int width = atoi(arg);

    if ((width < 40) || (width > 160))
    {
        send_to_char("You must enter a column number between 40 and 160.<br/>", ch);
        return;
    }

    act("Your screen width is now $2d columns.", A_ALWAYS, ch, &width, cActParameter(), TO_CHAR);

    UPC(ch)->getTerminalSetupType().width = (ubit8)width;

    MplexSendSetup(CHAR_DESCRIPTOR(ch));
}

static void chg_rows(unit_data *ch, const char *arg)
{
    if (str_is_empty(arg) || !str_is_number(arg))
    {
        send_to_char("You must enter a row number between 15 and 60.<br/>", ch);
        return;
    }

    int height = atoi(arg);

    if ((height < 15) || (height > 60))
    {
        send_to_char("You must enter a row number between 15 and 60.<br/>", ch);
        return;
    }

    UPC(ch)->getTerminalSetupType().height = (ubit8)height;

    act("Your screen height is $2d rows.", A_ALWAYS, ch, &height, cActParameter(), TO_CHAR);

    MplexSendSetup(CHAR_DESCRIPTOR(ch));
}

static void chg_terminal(unit_data *ch, const char *arg)
{
    const char *Terminals[] = {"dumb", "tty", "ansi", nullptr};

    char buf[1024];
    int n = 0;

    if (PC_SETUP_EMULATION(ch) == TERM_INTERNAL)
    {
        send_to_char("You can not change terminal mode at this time.<br/>", ch);
        return;
    }

    arg = one_argument(arg, buf);

    n = search_block(buf, Terminals, 0);

    switch (n)
    {
        case 0:
            UPC(ch)->getTerminalSetupType().emulation = TERM_DUMB;
            send_to_char("Now using no emulation (dumb).<br/>", ch);
            break;

        case 1:
            UPC(ch)->getTerminalSetupType().emulation = TERM_TTY;
            send_to_char("Now using TTY emulation.<br/>", ch);
            break;

        case 2:
            UPC(ch)->getTerminalSetupType().emulation = TERM_ANSI;
            send_to_char("Now using ANSI emulation.<br/>", ch);
            break;

        default:
            send_to_char("You must enter a terminal type: 'dumb', 'tty' or 'ansi'.<br/>", ch);
            return;
    }

    MplexSendSetup(CHAR_DESCRIPTOR(ch));
}

static void chg_telnet(unit_data *ch)
{
    if (PC_SETUP_EMULATION(ch) == TERM_INTERNAL)
    {
        send_to_char("You can not change telnet mode at this time.<br/>", ch);
        return;
    }

    UPC(ch)->getTerminalSetupType().telnet = !PC_SETUP_TELNET(ch);

    if (PC_SETUP_TELNET(ch))
    {
        act("You are now assumed to be using telnet.", A_ALWAYS, ch, cActParameter(), cActParameter(), TO_CHAR);
    }
    else
    {
        act("You are now assumed not to be using telnet.", A_ALWAYS, ch, cActParameter(), cActParameter(), TO_CHAR);
    }

    MplexSendSetup(CHAR_DESCRIPTOR(ch));
}

static void chg_character_echo(unit_data *ch)
{
    if (PC_SETUP_EMULATION(ch) == TERM_INTERNAL)
    {
        send_to_char("You can not change echo mode at this time.<br/>", ch);
        return;
    }

    UPC(ch)->getTerminalSetupType().echo = !PC_SETUP_ECHO(ch);

    if (PC_SETUP_ECHO(ch))
    {
        act("You will now get all typed characters echoed.", A_ALWAYS, ch, cActParameter(), cActParameter(), TO_CHAR);
    }
    else
    {
        act("You will now receive no echo characters.", A_ALWAYS, ch, cActParameter(), cActParameter(), TO_CHAR);
    }

    MplexSendSetup(CHAR_DESCRIPTOR(ch));
}

static void chg_redraw_prompt(unit_data *ch)
{
    if (PC_SETUP_EMULATION(ch) == TERM_INTERNAL)
    {
        send_to_char("You can not change redraw mode at this time.<br/>", ch);
        return;
    }

    UPC(ch)->getTerminalSetupType().redraw = !PC_SETUP_REDRAW(ch);

    if (PC_SETUP_REDRAW(ch))
    {
        act("You will now get your prompt redrawn.", A_ALWAYS, ch, cActParameter(), cActParameter(), TO_CHAR);
    }
    else
    {
        act("Your prompt will no longer get redrawn.", A_ALWAYS, ch, cActParameter(), cActParameter(), TO_CHAR);
    }

    MplexSendSetup(CHAR_DESCRIPTOR(ch));
}

static void chg_echo_say(unit_data *ch)
{
    UPC(ch)->togglePCFlag(PC_ECHO);

    if (IS_SET(PC_FLAGS(ch), PC_ECHO))
    {
        act("You will now get your communications echoed.", A_ALWAYS, ch, cActParameter(), cActParameter(), TO_CHAR);
    }
    else
    {
        act("You will no longer get your communications echoed.", A_ALWAYS, ch, cActParameter(), cActParameter(), TO_CHAR);
    }
}

void do_change(unit_data *ch, char *arg, const command_info *cmd)
{
    static const char *args[] = {"brief",
                                 "compact",
                                 "expert",
                                 "inform",
                                 "shout",
                                 "tell",
                                 "communications",

                                 "wimpy",
                                 "peaceful",

                                 "prompt",
                                 "exits",

                                 "columns",
                                 "rows",
                                 "terminal",
                                 "telnet",
                                 "character echo",
                                 "redraw prompt",
                                 nullptr};

    char buf[MAX_INPUT_LENGTH];

    if (!ch->isPC())
    {
        send_to_char("You don't want to do that.  Trust me.<br/>", ch);
        return;
    }

    if (!CHAR_DESCRIPTOR(ch))
    {
        return;
    }

    if (str_is_empty(arg))
    {
        send_to_char("Usage: change <type> [arguments]<br/>"
                     "<type> being one of:<br/>",
                     ch);

        for (const char **p = args; *p; p++)
        {
            auto msg = diku::format_to_str("   %s<br/>", *p);
            send_to_char(msg, ch);
        }

        return;
    }

    char *org_arg = arg;
    arg = one_argument(arg, buf);

    switch (search_block(buf, args, 0))
    {
        case 0:
            chg_brief(ch);
            break;

        case 1:
            chg_compact(ch);
            break;

        case 2:
            chg_expert(ch);
            break;

        case 3:
            chg_inform(ch);
            break;

        case 4:
            chg_shout(ch);
            break;

        case 5:
            chg_tell(ch);
            break;

        case 6:
            chg_echo_say(ch);
            break;

        case 7:
            chg_wimpy(ch);
            break;

        case 8:
            chg_peaceful(ch);
            break;

        case 9:
            chg_prompt(ch);
            break;

        case 10:
            chg_exits(ch);
            break;

        case 11:
            chg_columns(ch, arg);
            break;

        case 12:
            chg_rows(ch, arg);
            break;

        case 13:
            chg_terminal(ch, arg);
            break;

        case 14:
            chg_telnet(ch);
            break;

        case 15:
            chg_character_echo(ch);
            break;

        case 16:
            chg_redraw_prompt(ch);
            break;

        /*
           case 0:
            send_to_char("Enter a new password:<br/>", ch);
            echo_off(CHAR_DESCRIPTOR(ch));
            snoop_off(ch);
            push_inputfun(ch, new_pwd);
            break;

           case 1:
            send_to_char("Enter a text you'd like others to see when they look "
                         "at you.<br/>", ch);
            send_to_char("Terminate with a '@'.<br/><br/>", ch);
            push_inputfun(ch, new_description);
            break;

           case 2:
            send_to_char("Enter your email-adress, in the form name@host.<br/>", ch);
            push_inputfun(ch, new_email);
            break;

        */
        default:
            diltemplate *tmpl = nullptr;
            dilprg *prg = nullptr;

            tmpl = find_dil_template("do_change@commands");
            prg = dil_copy_template(tmpl, ch, nullptr);

            if (prg)
            {
                prg->waitcmd = WAITCMD_MAXINST - 1; // The usual hack, see db_file
                prg->fp->vars[0].val.string = str_dup(org_arg);
                dil_activate(prg);
            }
            break;
    }
}
