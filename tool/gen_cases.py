"""The comparison-case generator.

Builds every case in bernina_artista180/application/routines.json that was not
written by hand: the fill for each one, the register and stack set-up for the
original and for the rebuild, and the name it is reported under.

Run from the repository root:

    python3 tool/gen_cases.py

It writes the cases to /tmp/newcases.json; the merge step folds them into
routines.json, replacing any case of the same name and leaving the hand-written
ones alone.

A fill is a list of "address:length" -> byte keys applied *in order*, and the
order is the whole difficulty: a wide zero written after a narrow pin covers
it. audit() at the foot of this file walks every fill and says which pin a
later key shadows; drop() and pin= in the case helpers are how to fix one.
"""
import json, collections, sys, os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

P='bernina_artista180/application/routines.json'
import spec_fills
d=spec_fills.load(P)          # fills expanded: BASE below wants a whole one
cases=d['cases']
# By name, not by position: once the generated cases are in the file the
# last one is one of them, and every fill would be built on itself.
BASE = [c for c in cases
        if c['name'] == 'screen_request (asked for 7B while on it)'][0]
SEED = dict(BASE.get('seed',{}))

def move_wipe(case, key='0E9000:8000', after='0E9000:2000'):
    """Puts the wide catalogue zero where it belongs: right after the base
    fill's own, and before everything this case sets.

    A key the fill does not already have is appended at the end, so a wide
    zero added by a case lands after its own narrow pins and wipes them.
    Only the ordering moves; the value is the same."""
    f = case['fill']
    if key not in f:
        return
    v = f.pop(key)
    out = collections.OrderedDict()
    for k, val in f.items():
        out[k] = val
        if k == after:
            out[key] = v
    if key not in out:
        out[key] = v
    case['fill'] = out

def put(f, d):
    """Sets keys, keeping the order the shared fill has.

    Order matters: the fill is applied in order and the shared one wipes
    several wide ranges part-way through, so a key that is already in it
    keeps its place and a later wide zero can still cover it. Setting a key
    that the base already has therefore changes its value and nothing else
    -- which is what is wanted, because moving it to the end changes what
    else the fill covers and some of those changes send the body into a
    loop it never leaves."""
    f.update(d)
    return f

def base_fill(extra=None):
    f=collections.OrderedDict(BASE['fill'])
    if extra: put(f, extra)
    return f

def w16(addr, v):
    return {'%06X:1'%addr: '%02X'%((v>>8)&0xFF), '%06X:1'%(addr+1): '%02X'%(v&0xFF)}
def w32(addr, v):
    return {'%06X:1'%(addr+i): '%02X'%((v>>(8*(3-i)))&0xFF) for i in range(4)}
def b8(addr, v):
    return {'%06X:1'%addr: '%02X'%(v&0xFF)}

def audit(case):
    """Narrow pins a later wide key covers.

    The fill is applied in order and a key the base already has keeps the
    base's position, so a wide zero added afterwards can wipe a value set
    earlier in the same fill. Every hour lost to this has been the same
    mistake; this says so instead."""
    keys = list(case['fill'].keys())
    cover = {}
    for i, k in enumerate(keys):
        a, span = k.split(':')
        at, n = int(a, 16), int(span, 16)
        for x in range(at, at + n):
            cover[x] = i
    bad = []
    for i, k in enumerate(keys):
        a, span = k.split(':')
        at, n = int(a, 16), int(span, 16)
        if n != 1:
            continue
        if cover[at] != i and case['fill'][k] != case['fill'][keys[cover[at]]]:
            bad.append((k, keys[cover[at]]))
    return bad

new=[]
def add(name, fill, orig, rebuilt, seed=None, steps=None):
    c=collections.OrderedDict()
    c['name']=name; c['boot']=1900000
    if steps: c['steps']=steps
    c['seed']=SEED if seed is None else seed
    c['fill']=fill
    c['original']=orig; c['rebuilt']=rebuilt
    new.append(c)

# ---------------------------------------------------------------- H'211A02
HOLD = {'11A166:2':'00', '114DE0:2':'00', '11A171:1':'00', '11A180:2':'00',
        '0E8010:100':'00'}
def hold_case(name, hold, tick, msg, covered):
    f=base_fill(dict(HOLD))
    put(f, w16(0x11A166,hold)); put(f, w16(0x114DE0,tick))
    put(f, w16(0x11A180,msg));  put(f, b8(0x11A171,covered))
    add('message_hold_done (%s)'%name, f,
        {'addr':'211A02','result':'r6l'},
        {'symbol':'_message_hold_done','result':'r0l'})

hold_case('nothing held',             0x0000, 0x0040, 0x0003, 0x00)
hold_case('still holding',            0x0100, 0x0040, 0x0003, 0x00)
hold_case('exactly there',            0x0040, 0x0040, 0x0003, 0x00)
hold_case('time up, box lit',         0x0040, 0x0100, 0x0003, 0x00)
hold_case('time up, no box',          0x0040, 0x0100, 0x0000, 0x00)
hold_case('time up, screen covered',  0x0040, 0x0100, 0x0005, 0x01)
hold_case('time up, covered, no box', 0x0040, 0x0100, 0x0000, 0x01)

# ---------------------------------------------------------------- H'212B5E
# Boxes 5 to 9 rather than 1 to 5: the shared fill wipes the box table again
# part-way through, so only the boxes defined after that survive it.
LIST = collections.OrderedDict()
LIST.update(w16(0x11A88E, 0x0004))      # four items
LIST.update(w16(0x11A890, 0x0001))
LIST.update(w16(0x11A892, 0x0002))
LIST.update(w16(0x11A894, 0x0003))
LIST.update(w16(0x11A896, 0x0000))      # a hole
LIST.update(w16(0x11A898, 0x0005))
for r in range(8):                      # every record points at a real picture
    LIST.update(w32(0x0E9000 + 0x18*r + 0x08, 0x0034C6CD))
    # and has data behind it, which is what "the first of this category"
    # looks for -- with all four pointers zero no category has anything and
    # the search always answers one, whatever it was asked for.
    LIST.update(w32(0x0E9000 + 0x18*r + 0x04, 0x0034C6CD))
LIST.update(b8(0x0E9000 + 0x18*3 + 0x17, 0x03))   # item three, category three
STYLE7 = {'0E208F:1': '01'}             # box 7 in a style this does not draw

def fill_case(name, first, last, value, extra=None):
    f=base_fill(collections.OrderedDict(LIST))
    if extra: put(f, extra)
    add('menu_list_fill (%s)'%name, f,
        {'addr':'212B5E','result':'r6','regs':{'er6':'%04X'%first},
         'stack':{'4':'2:%04X'%last, '6':'2:%04X'%value}},
        {'symbol':'_menu_list_fill','result':'r0',
         'regs':{'er0':'%04X'%first,'er1':'%04X'%last,'er2':'%04X'%value}})

fill_case('boxes 5 to 9, from item 1', 5, 9, 1)
fill_case('boxes 5 to 9, from the length word', 5, 9, 0)
fill_case('first past last', 9, 5, 1)
fill_case('one box only', 6, 6, 2)
fill_case('every one past the end', 5, 9, 0x000A)
fill_case('a box in another style, in range', 5, 9, 1, STYLE7)
fill_case('a box in another style, past the end', 5, 9, 5, STYLE7)
fill_case('boxes 1 to 5, none of them drawn', 1, 5, 1)

# ---------------------------------------------------------------- H'212C60
# The queue screen's second run of boxes, H'20 to H'26, given a style and
# coordinates of their own -- the shared fill leaves them blank, and blank
# boxes draw nothing whatever the run is, which hides the ends of it.
STRIP = collections.OrderedDict()
for k in range(7):
    e = 0x0E2000 + 0x12*(0x20+k)
    STRIP.update(w16(e+0x00, 0x0008 + 0x2C*k))
    STRIP.update(w16(e+0x02, 0x0060))
    STRIP.update(w16(e+0x04, 0x0030 + 0x2C*k))
    STRIP.update(w16(e+0x06, 0x0078))
    STRIP.update(w16(e+0x08, k))     # distinct, so a search over the
                                     # run can only match one of them
    STRIP.update(b8(e+0x0A, 0x00))
    STRIP.update(w32(e+0x0C, 0x0011A88E))
    STRIP.update(b8(e+0x10, 0x00))
    STRIP.update(b8(e+0x11, 0x03))

# A longer list than the one H'212B5E is tried with, so that the boxes the
# shift lands in have something to draw and stay out of state 2 -- a run of
# greyed boxes is skipped by the search, which hides where the run ends.
REPICK_LIST = collections.OrderedDict()
REPICK_LIST.update(w16(0x11A88E, 0x0014))
for v in range(1, 0x15):
    REPICK_LIST.update(w16(0x11A88E + 2*v, (v % 7) + 1))
REPICK_LIST.update(w16(0xFFFEE0, 0x0005))   # the item box H'21 carries

def repick_case(name, screen, category, extra=None):
    f=base_fill(collections.OrderedDict(LIST))
    put(f, STRIP)
    put(f, REPICK_LIST)
    put(f, b8(0x11A169, screen))
    put(f, b8(0x11B28E, category))
    put(f, b8(0x11A170, 0x01))
    if extra: put(f, extra)
    add('menu_repick (%s)'%name, f, {'addr':'212C60'}, {'symbol':'_menu_repick'})

repick_case('the sewing screen',                       0x02, 0x00)
repick_case('the sewing screen, another category',     0x02, 0x03)
repick_case('the queue screen',                        0x07, 0x00)
repick_case('the queue screen, another category',      0x07, 0x03)
repick_case('some other screen',                       0x05, 0x00)
repick_case('the sewing screen, a box in another style', 0x02, 0x00, STYLE7)

# ---------------------------------------------------------------- H'223A50
# The four longwords the body copies out of H'116A1A land on H'11B0AE, and
# the last of them *is* the hit-box table pointer -- so it has to be the
# table, or everything after the copy works on rubbish. The first is the
# background, given a three-run picture of its own here.
BODY = collections.OrderedDict()
BODY.update(w16(0x0E9800, 0x0003))          # a background of three runs
BODY.update(w16(0x0E9802, 0x0455))
BODY.update(w16(0x0E9804, 0x0322))
BODY.update(w32(0x00116A1A, 0x000E9800))
# The second longword lands on H'11B0B2 and H'11B0B4, which are the screen
# origin every box is drawn relative to; zero puts the origin at the corner.
BODY.update(w32(0x00116A1E, 0x00000000))
BODY.update(w32(0x00116A22, 0x00000000))
BODY.update(w32(0x00116A26, 0x000E2000))    # the box table, where it belongs
BODY['115A06:40']='00'
BODY.update(w16(0x00115A06, 0x0004))        # the four category boxes' list
for v in range(1, 5):
    BODY.update(w16(0x00115A06 + 2*v, v))
# The parked screen change, for the case that takes it: H'11B29A is the
# screen it goes back to, and without pinning it the restore lands on
# whichever screen the boot happened to leave there.
BODY['11B290:20']='00'
BODY.update(b8(0x0011B29A, 0x02))
# The needle-stop picture, which is otherwise a null pointer whose header
# gives an empty picture -- and an empty picture draws nothing, so leaving
# it out would look the same as putting it in.
BODY.update(w32(0x00115892, 0x0034C6CD))
# The item the machine is on, and a category for it. Left at zero the
# category is below the three the panel-strip table starts at, so H'21F9D0
# picks no strip at all and leaving it out would look the same; and box one
# is the box the current-item search should land on. H'12 is a category
# that both picks a strip and draws its preview at fixed coordinates --
# the ones that scale a picture to the panel put a null pointer's H'400-row
# header outside the buffer.
BODY.update(w16(0x00FFFEE0, 0x0002))
BODY.update(b8(0x0E9000 + 0x18*2 + 0x17, 0x12))
for r in range(8):
    BODY.update(w32(0x0E9000 + 0x18*r + 0x0C, 0x0034C6CD))

# The cursor: H'11A1E9 is what mode 4 means, and left at zero every cursor
# call in the body settles into "already put away" and does nothing.
# The first word of the table is how many boxes there are; the shared fill
# wipes it, and with no boxes "put every box back" is nothing at all. Six
# is enough to make it something without every box in the table being
# redrawn on every pass.
BODY.update(w16(0x000E2000, 0x0006))
# A few boxes left pressed, so that putting every box back to plain is
# something rather than nothing.
for k in range(1, 6):
    BODY.update(b8(0x0E2000 + 0x12*k + 0x10, 0x01))
BODY['11B3C0:20']='00'
BODY.update(b8(0x0011A1E9, 0x01))
BODY.update(w16(0x0011B108, 0x0001))
BODY.update(w16(0x0011B10C, 0x0001))
BODY.update(b8(0x00FFFEE7, 0x32))           # the width the bar shows
BODY.update(b8(0x00FFFEE4, 0x28))           # and the length
# What the two bars remember of last time. Without these the pass that is
# not a redraw works from whatever the boot left, and the two images do not
# leave the same thing -- a rectangle drawn to y = H'FFFF lands outside the
# buffer at a different place on each side.
BODY['11A840:4E']='00'                      # up to, not including, the list
BODY.update(w16(0x0011A85E, 0x0028))        # the width it last drew
BODY.update(w16(0x0011A860, 0x006D))        # and where the top of it was
BODY.update(w16(0x0011A872, 0x00F3))        # the length's right-hand end
BODY.update(w16(0x0011A874, 0x0020))        # and the length itself

# ---------------------------------------------------------------- H'22382A
# The body cannot be called on its own: it ends by branching into the
# dispatcher's tail, which pops five registers the dispatcher's prologue
# pushed. So the body is exercised through H'22382A, which is how the
# machine reaches it anyway.
def dispatch_case(name, screen, arrived, relayout, queue, menukey=0,
                  held=0, touch='0000', parked=0, foot=0, settled=False,
                  extra=None):
    f=base_fill(collections.OrderedDict(LIST))
    put(f, STRIP); put(f, REPICK_LIST); put(f, BODY)
    put(f, b8(0x11A169, screen))
    put(f, b8(0x11B0A8, arrived))
    put(f, b8(0x11B0A9, relayout))
    put(f, b8(0x11A174, queue))
    put(f, b8(0x11A170, menukey))
    put(f, b8(0x11B28E, 0x00))
    put(f, w16(0x11A166, held))
    put(f, w16(0x114DE0, 0x0040))
    put(f, b8(0x11B29C, parked))
    put(f, b8(0x11A16E, foot))
    put(f, {'11B10E:1': touch[:2], '11B10F:1': touch[2:]})
    if settled:
        # H'210E02 only says "not settled" once its countdown is running and
        # the two coordinate bytes have stopped moving; left as the boot has
        # it the countdown is zero and it says "settled" on the first call,
        # which is what makes the early exit unreachable.
        put(f, w16(0x11A19A, 0x0005))
        put(f, w16(0x11B2CA, 0x0000))
        put(f, w16(0x11B2CC, 0x0000))
    if extra: put(f, extra)
    add('screen_dispatch (%s)'%name, f, {'addr':'22382A'},
        {'symbol':'_screen_dispatch'})

dispatch_case('the sewing screen, just arrived',       0x02, 1, 0, 0)
dispatch_case('the sewing screen, laid out again',     0x02, 0, 1, 0)
dispatch_case('the sewing screen, a plain pass',       0x02, 0, 0, 0)
dispatch_case('the sewing screen, a menu key waiting', 0x02, 0, 0, 0, 1)
dispatch_case('the queue screen, just arrived',        0x07, 1, 0, 1)
dispatch_case('the queue screen, laid out again',      0x07, 0, 1, 1)
dispatch_case('the queue screen, a plain pass',        0x07, 0, 0, 1)
dispatch_case('the queue screen, no queue strip',      0x07, 1, 0, 0)
dispatch_case('a message still being held',            0x02, 1, 0, 0, held=0x0100)
dispatch_case('nothing settled yet',                   0x02, 1, 0, 0, touch='FFFF')
dispatch_case('a settled touch, nothing asked for',    0x02, 1, 0, 0, touch='FFFF',
              settled=True)
dispatch_case('a settled touch, something asked for',  0x02, 1, 0, 0, touch='0001',
              settled=True)
dispatch_case('a parked screen change',                0x02, 1, 0, 0, parked=1)
dispatch_case('the foot switch',                       0x02, 1, 0, 0, foot=1)
dispatch_case('a screen the table does not cover',     0x50, 1, 0, 0)
# The fourth longword the arrival copies is the hit-box table pointer, and
# with the table already pinned to the same value copying it changes
# nothing. Pointed somewhere blank to begin with, it does.
dispatch_case('just arrived, the table pointer moved',  0x02, 1, 0, 0,
              extra=w32(0x0011B0BA, 0x000E5000))

# ---------------------------------------------------------------- H'223CCA
# The menu screens' own background block, and the second menu list the
# twenty-box strip is filled from. Kept apart from BODY so the screen H'02
# cases keep the fill they were checked with.
BODY3 = collections.OrderedDict()
BODY3['116FD0:10']='00'
BODY3.update(w32(0x00116FD0, 0x000E9800))   # the background
BODY3.update(w32(0x00116FD4, 0x00000000))   # the origin, at the corner
BODY3.update(w32(0x00116FD8, 0x00000000))
BODY3.update(w32(0x00116FDC, 0x000E2000))   # and the box table
BODY3.update(w32(0x0011B096, 0x0011A88E))   # the second menu list

def dispatch3_case(name, screen, arrived, relayout, queue, extra=None):
    e = collections.OrderedDict(BODY3)
    if extra: e.update(extra)
    dispatch_case(name, screen, arrived, relayout, queue, extra=e)
    new[-1]['name'] = 'screen_body_03 (%s)'%name

dispatch3_case('the menu screen, just arrived',      0x03, 1, 0, 0)
dispatch3_case('the menu screen, laid out again',    0x03, 0, 1, 0)
dispatch3_case('the menu screen, a plain pass',      0x03, 0, 0, 0)
dispatch3_case('the menu screen with the queue',     0x04, 1, 0, 1)
dispatch3_case('the menu screen with the queue, laid out again',
               0x04, 0, 1, 1)
dispatch3_case('the menu screen with the queue, a plain pass',
               0x04, 0, 0, 1)
dispatch3_case('the menu screen with the queue, no queue strip',
               0x04, 1, 0, 0)
dispatch3_case('the menu screen, the table pointer moved', 0x03, 1, 0, 0,
               extra=w32(0x0011B0BA, 0x000E5000))

# ---------------------------------------------------------------- the queue's list
# The three lists a delete moves, and the tables behind them. H'11B212 is
# what the strip shows, H'11B11E the patterns behind it and H'11B198 the
# slots they are written to; H'11B11C and H'11B28C are what says where the
# category-one runs end.
QLIST = collections.OrderedDict()
QLIST['11B212:40']='00'
QLIST['11B11E:40']='00'
QLIST['11B198:40']='00'
QLIST.update(w16(0x0011B11C, 0x0010))       # the base index
QLIST.update(w16(0x0011B28C, 0x0008))       # and the run beyond it
QLIST.update(w16(0x0011B212, 0x0006))       # six positions in the strip
for v in range(1, 7):
    QLIST.update(w16(0x0011B212 + 2*v, 0x0010 + v))
QLIST.update(w16(0x0011B11E, 0x0008))       # eight patterns behind them
for v in range(1, 9):
    QLIST.update(w16(0x0011B11E + 2*v, 0x0020 + v))
QLIST.update(w16(0x0011B198, 0x0008))
for v in range(1, 9):
    QLIST.update(w16(0x0011B198 + 2*v, 0x0030 + v))
# Only H'14 is category one, so position three carries a run of two and
# every other position stands on its own. The runs have to add up to no
# more than the pattern lists are long: the delete indexes those lists by
# the position plus what the runs before it cost, and an index past the end
# makes list_delete's length underflow into a 64K move.
for r in range(0x10, 0x1A):
    QLIST.update(b8(0x0E9000 + 0x18*r + 0x17, 0x01 if r == 0x14 else 0x00))
# The table's terminator. H'223010 rebuilds both item lists from it, and
# without one first_index_of_category answers H'FFFF -- the entry is then
# H'18 * H'FFFF past the table and the length it reads is whatever happens
# to be there.
QLIST.update(b8(0x0E9000 + 0x18*0x0A + 0x17, 0x02))
QLIST.update(w16(0x0E9000 + 0x18*0x0A + 0x14, 0x0004))

def q_case(name, orig, rebuilt, extra=None):
    f=base_fill(collections.OrderedDict(LIST))
    put(f, QLIST)
    if extra: put(f, extra)
    add(name, f, orig, rebuilt)

for first, last in ((1, 3), (1, 1), (2, 2), (1, 6), (3, 1), (1, 0)):
    q_case('queue_run_extra (%d to %d)'%(first, last),
           {'addr':'2107D4','result':'r6','regs':{'er6':'%04X'%first},
            'stack':{'4':'2:%04X'%last}},
           {'symbol':'_queue_run_extra','result':'r0',
            'regs':{'er0':'%04X'%first,'er1':'%04X'%last}})

for at, why in ((0x0001, 'the first position, which cannot go'),
                (0x0002, 'a position of its own'),
                (0x0003, 'the position that carries a run'),
                (0x0004, 'the position just past a run'),
                (0x0006, 'the last position'),
                (0x0000, 'nothing selected')):
    q_case('queue_entry_delete (%s)'%why,
           {'addr':'210B58','result':'r6'},
           {'symbol':'_queue_entry_delete','result':'r0'},
           extra=w16(0x0011A186, at))

# ---------------------------------------------------------------- the flash copies
# These two write through the boot ROM's flash writer, so the table has to
# be where the flash is -- H'0E9000, which the fill above puts it at for the
# sake of the screens, is RAM and the writer would be programming nothing.
FSEED = {'114DD2':'00','114DD3':'50','114DD4':'00','114DD5':'00'}
FFILL = collections.OrderedDict()
FFILL['0E4010:800']='00'
FFILL['11B11E:40']='00'
FFILL['11B198:40']='00'
FFILL.update(w16(0x0011B11C, 0x0020))
for k in range(16):                          # something to copy
    FFILL.update(b8(0x0E4010 + 0x10*0x20 + k, 0x11 + k))
    FFILL.update(b8(0x0E4010 + 0x10*0x21 + k, 0xA0 + k))

def flash_case(name, orig, rebuilt, extra=None):
    f=collections.OrderedDict(FFILL)
    if extra: f.update(extra)
    add(name, f, orig, rebuilt, seed=FSEED)

for a, b in ((0x0020, 0x0021), (0x0021, 0x0021), (0x0001, 0x0002)):
    flash_case('item_descriptor_copy (%02X to %02X)'%(a, b),
               {'addr':'210A22','regs':{'er6':'%04X'%a},'stack':{'4':'2:%04X'%b}},
               {'symbol':'_item_descriptor_copy','regs':{'er0':'%04X'%a,'er1':'%04X'%b}})
    flash_case('item_records_copy (%02X to %02X)'%(a, b),
               {'addr':'200D44','regs':{'er6':'%04X'%a},'stack':{'4':'2:%04X'%b}},
               {'symbol':'_item_records_copy','regs':{'er0':'%04X'%a,'er1':'%04X'%b}})

RENUM = collections.OrderedDict()
RENUM.update(w16(0x0011B11E, 0x0002))
RENUM.update(w16(0x0011B120, 0x0020))
RENUM.update(w16(0x0011B122, 0x0021))
# Three, where the other list has two: the run length is set from one of
# them and the loop runs over the other, and with both the same the two
# cannot be told apart.
RENUM.update(w16(0x0011B198, 0x0003))
RENUM.update(w16(0x0011B19A, 0x0022))
RENUM.update(w16(0x0011B19C, 0x0023))
RENUM.update(w16(0x0011B19E, 0x0024))
flash_case('queue_items_renumber (two of them)',
           {'addr':'2109AE'}, {'symbol':'_queue_items_renumber'}, extra=RENUM)
EMPTY = collections.OrderedDict()
EMPTY.update(w16(0x0011B11E, 0x0000))
flash_case('queue_items_renumber (none)',
           {'addr':'2109AE'}, {'symbol':'_queue_items_renumber'}, extra=EMPTY)

# ---------------------------------------------------------------- H'22301A
# The touch, scaled one for one so that H'FFFED9 and H'FFFEDA are the pixel
# the press is on, and the queue screen's three keys given boxes of their
# own -- the shared fill wipes that end of the table.
TOUCH = collections.OrderedDict()
TOUCH.update(w32(0x0011A87E, 0x3F800000))   # x scale, 1.0
TOUCH.update(w32(0x0011A882, 0x3F800000))   # y scale
TOUCH.update(w32(0x0011A886, 0x00000000))   # and no offset either way
TOUCH.update(w32(0x0011A88A, 0x00000000))
TOUCH['11F547:2']='00'
TOUCH['11F549:1']='00'
TOUCH['11A19C:4']='00'
TOUCH['11B2CE:2']='00'
TOUCH['11A1A0:4']='00'
TOUCH['11B102:4']='00'
# A hit-box table of its own, at H'0E5000. The shared fill's table is wiped
# part-way through and the boxes before H'05 never come back, so a press
# that has to land on box one cannot be set up in it at all.
PRESS_TABLE = 0x000E5000
STRIPBOX = collections.OrderedDict()
STRIPBOX['0E5000:200']='00'
STRIPBOX.update(w32(0x0011B0BA, PRESS_TABLE))
STRIPBOX.update(w16(PRESS_TABLE, 0x0030))
for i in range(1, 0x10):
    e = PRESS_TABLE + 0x12*i
    STRIPBOX.update(w16(e+0x00, 0x0008*i))
    STRIPBOX.update(w16(e+0x02, 0x0060))
    STRIPBOX.update(w16(e+0x04, 0x0006 + 0x0008*i))
    STRIPBOX.update(w16(e+0x06, 0x0070))
    STRIPBOX.update(w16(e+0x08, 0x0003 if i == 2 else 0x0010 + i))
    STRIPBOX.update(b8(e+0x0A, 0x01 if i == 2 else 0x00))
    # Box two is the one with a list and the "second number" flag, which is
    # the only way into the H'44 arm of the search -- a box with no list is
    # matched on its own value and never gets there.
    STRIPBOX.update(w32(e+0x0C, 0x0011B212 if i == 2 else 0x00000000))
    STRIPBOX.update(b8(e+0x10, 0x00))
    STRIPBOX.update(b8(e+0x11, 0x03))

def keyboxes(table):
    """The three keys H'26 to H'28, in whichever table is in use."""
    d = collections.OrderedDict()
    for k, value in enumerate((0x0019, 0x0010, 0x000D)):
        e = table + 0x12*(0x26+k)
        d.update(w16(e+0x00, 0x0010 + 0x30*k))
        d.update(w16(e+0x02, 0x0020))
        d.update(w16(e+0x04, 0x0030 + 0x30*k))
        d.update(w16(e+0x06, 0x0040))
        d.update(w16(e+0x08, value))
        d.update(b8(e+0x0A, 0x00))
        d.update(w32(e+0x0C, 0x00000000))
        d.update(b8(e+0x10, 0x00))
        d.update(b8(e+0x11, 0x03))
    return d

KEYBOX = keyboxes(0x000E2000)
KEYBOX_P = keyboxes(PRESS_TABLE)
_unused = collections.OrderedDict()
for k, value in enumerate((0x0019, 0x0010, 0x000D)):
    e = PRESS_TABLE + 0x12*(0x26+k)
    _unused.update(b8(e+0x11, 0x03))

def press_case(name, to, at, x=0x00, y=0x00, extra=None):
    f=base_fill(collections.OrderedDict(LIST))
    put(f, QLIST); put(f, TOUCH); put(f, STRIPBOX); put(f, KEYBOX_P)
    put(f, b8(0x0011A169, 0x44))             # the screen this belongs to
    put(f, w16(0x0011A188, 0x1234))          # never what a box carries
    for r in range(0x31):
        put(f, w32(0x0E9000 + 0x18*r + 0x04, 0x0034C6CD))
        put(f, w32(0x0E9000 + 0x18*r + 0x08, 0x0034C6CD))
        put(f, w32(0x0E9000 + 0x18*r + 0x0C, 0x0034C6CD))
        put(f, b8(0x0E9000 + 0x18*r + 0x17,
                  0x02 if r == 0x0A else 0x01 if r == 0x14 else 0x12))
    put(f, w16(0x0011B10E, to))
    put(f, w16(0x0011A17E, 0xFFFE))          # never the screen being asked for
    put(f, w16(0x0011A186, at))
    put(f, w16(0x0011B108, 0x0002))
    put(f, w16(0x00FFFEE0, 0x0013))          # the item box three carries
    put(f, b8(0x0011A17C, 0x00))
    # Not zero: screen_switch restarts the tick, and a side that writes zero
    # over a zero it cannot see is indistinguishable from one that does not
    # write at all.
    put(f, w16(0x00114DE0, 0x1234))
    put(f, b8(0x00FFFED9, x))
    put(f, b8(0x00FFFEDA, y))
    if extra: put(f, extra)
    add('queue_edit_press (%s)'%name, f,
        {'addr':'22301A'}, {'symbol':'_queue_edit_press'})

press_case('the strip, a box past the first',   0x0077, 0x0003)
press_case('the strip, nothing selected',       0x0077, 0x0001)
press_case('the strip, no box carries it',      0x0077, 0x0003,
           extra=w16(0x00FFFEE0, 0x7FFF))
press_case('the strip, the first box',          0x0077, 0x0003,
           extra=w16(0x00FFFEE0, 0x0011))
press_case('the strip, the first box and a greyed second', 0x0077, 0x0003,
           extra=dict(list(w16(0x00FFFEE0, 0x0011).items()) +
                      list(b8(PRESS_TABLE + 0x12*2 + 0x10, 0x02).items())))
press_case('the strip, the last box',           0x0077, 0x0003,
           extra=w16(0x00FFFEE0, 0x001F))
press_case('the strip, the box told apart by the second number',
           0x0077, 0x0003, extra=w16(0x00FFFEE0, 0x7FFE))
press_case('the strip, the cursor on the last position', 0x0077, 0x0005)
press_case('the screen it is already on',       0x0077, 0x0003,
           extra=w16(0x0011A17E, 0x0077))
press_case('the done key',                      0xFFFF, 0x0003, 0x20, 0x30)
# The done key again, with everything it touches set so that it can be seen
# doing it: a stack to pop, a remembered screen that matches the one it is
# going to, and a walk position with its top bit up so that a signed
# comparison would answer differently from the unsigned one.
press_case('the done key, with a stack and a remembered screen',
           0xFFFF, 0x0003, 0x20, 0x30,
           extra=dict(list(b8(0x0011A18B, 0x02).items()) +
                      list(b8(0x0011A18C, 0x30).items()) +
                      list(b8(0x0011A18D, 0x31).items()) +
                      list(b8(0x0011A16A, 0x27).items()) +
                      list(b8(0x0011B0A8, 0x01).items()) +
                      list(w16(0x00FFFEE0, 0xFFF0).items())))
press_case('the renumber key',                  0xFFFF, 0x0003, 0x50, 0x30)
press_case('the third key',                     0xFFFF, 0x0003, 0x80, 0x30)
press_case('a press on none of them',           0xFFFF, 0x0003, 0xB0, 0x30)
press_case('no press at all',                   0xFFFF, 0x0003, 0x00, 0x00)
press_case('a screen asked for that is not the strip', 0x0031, 0x0003, 0x20, 0x30)

# ---------------------------------------------------------------- H'223F2A
BODY30 = collections.OrderedDict()
BODY30['116D0C:10']='00'
BODY30.update(w32(0x00116D0C, 0x000E9800))   # the background
BODY30.update(w32(0x00116D10, 0x00000000))   # the origin
BODY30.update(w32(0x00116D14, 0x00000000))
BODY30.update(w32(0x00116D18, 0x000E2000))   # the box table
BODY30['116D1C:20']='00'                     # the three keys' own list
BODY30.update(w16(0x00116D1C, 0x0003))
for v in range(1, 4):
    BODY30.update(w16(0x00116D1C + 2*v, v))
BODY30.update(w16(0x0011B10A, 0x0001))       # where the strip starts
BODY30.update(b8(0x0011A179, 0x01))          # a message may go up
BODY30.update(b8(0x0011A173, 0x00))
# A strip list long enough that the page the walk is on is not page one:
# with fifteen or fewer positions H'222FD2 always answers one, and "the page
# the walk is on" cannot be told from a constant.
# No wide key here: the queue fill above has already zeroed the list, and a
# wide key added now would land after its entries and wipe them again.
BODY30.update(w16(0x0011B212, 0x0014))
for v in range(1, 0x15):
    BODY30.update(w16(0x0011B212 + 2*v, 0x0010 + v))
# The number of boxes, which "put every box back" runs over. H'0E2000 and
# H'0E2001 are both set by the shared fill before it wipes the table again,
# so they cannot be set here; H'0E2001:2 is a key the shared fill does not
# have, so it lands after the wipe. Its second byte is box zero's y0, which
# nothing uses.
BODY30['0E2001:2']='30'
for r in range(0x31):                        # every item drawable
    BODY30.update(w32(0x0E9000 + 0x18*r + 0x04, 0x0034C6CD))
    BODY30.update(w32(0x0E9000 + 0x18*r + 0x08, 0x0034C6CD))
    BODY30.update(w32(0x0E9000 + 0x18*r + 0x0C, 0x0034C6CD))

def dispatch30_case(name, screen, arrived, relayout, queue, full=0,
                    x=0x00, y=0x00, to=0xFFFF, extra=None):
    e = collections.OrderedDict(QLIST)
    e.update(TOUCH); e.update(KEYBOX); e.update(BODY30)
    e.update(b8(0x0011A18A, full))
    e.update(b8(0x0011A186 + 1, 0x03))       # a position to work from
    e.update(b8(0x00FFFED9, x))
    e.update(b8(0x00FFFEDA, y))
    e.update(b8(0x0011A17C, 0x00))
    e.update(w16(0x00114DE0, 0x1234))
    if extra: e.update(extra)
    dispatch_case(name, screen, arrived, relayout, queue, touch='%04X'%to,
                  extra=e)
    new[-1]['name'] = 'screen_body_30 (%s)'%name

dispatch30_case('the queue screen, just arrived',   0x30, 1, 0, 0)
dispatch30_case('the queue screen, laid out again', 0x30, 0, 1, 0)
dispatch30_case('the queue screen, a plain pass',   0x30, 0, 0, 0)
dispatch30_case('the editing screen, just arrived', 0x44, 1, 0, 0)
dispatch30_case('the editing screen, laid out again', 0x44, 0, 1, 0)
dispatch30_case('the editing screen, a plain pass', 0x44, 0, 0, 0)
dispatch30_case('the editing screen, the queue full', 0x44, 0, 1, 0, full=1)
dispatch30_case('the editing screen, the done key', 0x44, 0, 0, 0,
                x=0x20, y=0x30)
dispatch30_case('the editing screen, the strip pressed', 0x44, 0, 0, 0,
                to=0x0077)
dispatch30_case('the queue screen with the strip', 0x45, 1, 0, 1)
dispatch30_case('the queue screen with the strip, a plain pass',
                0x45, 0, 0, 1)
dispatch30_case('the queue screen, the table pointer moved', 0x30, 1, 0, 0,
                extra=w32(0x0011B0BA, 0x000E5000))

# ------------------------------------------------- the plainer screens
# A background block of the same shape as the others: the picture, the two
# corners the copy takes, and the box table.
def block(at, picture=0x000E9800):
    d = collections.OrderedDict()
    d['%06X:10'%at]='00'
    d.update(w32(at + 0x00, picture))
    d.update(w16(at + 0x04, 0x0004))        # x0, y0
    d.update(w16(at + 0x06, 0x0000))
    d.update(w16(at + 0x08, 0x0028))        # x1, y1 -- not square, so that
    d.update(w16(at + 0x0A, 0x0014))        # the four are told apart
    d.update(w32(at + 0x0C, PLAIN_TABLE))   # the box table
    return d

# A hit-box table of its own for these screens, with real boxes in it. The
# shared fill's table has its first four boxes wiped, and once "put every box
# back" has given them style three a blank box is drawn -- at coordinates of
# zero, which the inset takes two pixels outside and off the buffer.
PLAIN_TABLE = 0x000E5000
PLAINBOX = collections.OrderedDict()
PLAINBOX['0E5000:400']='00'
PLAINBOX.update(w16(PLAIN_TABLE, 0x0030))
for i in range(1, 0x31):
    e = PLAIN_TABLE + 0x12*i
    PLAINBOX.update(w16(e+0x00, 0x0008 + 0x0010*((i-1) % 0x0C)))
    PLAINBOX.update(w16(e+0x02, 0x0020 + 0x0010*((i-1) // 0x0C)))
    PLAINBOX.update(w16(e+0x04, 0x0010 + 0x0010*((i-1) % 0x0C)))
    PLAINBOX.update(w16(e+0x06, 0x0028 + 0x0010*((i-1) // 0x0C)))
    PLAINBOX.update(w16(e+0x08, 0x0002 if i == 5 else i))
    PLAINBOX.update(b8(e+0x0A, 0x00))
    PLAINBOX.update(w32(e+0x0C, 0x00000000))
    PLAINBOX.update(b8(e+0x10, 0x01 if i == 1 else 0x00))
    PLAINBOX.update(b8(e+0x11, 0x03))

# The picture every one of these blocks points at. It has to be re-stated
# after the wide zero over the catalogue, which covers H'0E9800 too -- and
# an unpacked picture of no runs writes nothing at all, which would make a
# wrong destination look the same as the right one.
RUNLIST = collections.OrderedDict()
RUNLIST.update(w16(0x0E9800, 0x0003))
RUNLIST.update(w16(0x0E9802, 0x0455))
RUNLIST.update(w16(0x0E9804, 0x0322))

def plain_case(name, screen, arrived, relayout, blocks, x=0x00, y=0x00,
               extra=None):
    e = collections.OrderedDict()
    # The whole catalogue, not just the part the narrow pins reach: the
    # panel strip's lists hold real pattern numbers, and a descriptor read
    # past the end of what the shared fill covers is whatever the boot left.
    e['0E9000:8000']='00'
    for at in blocks:
        e.update(block(at))
    e.update(RUNLIST)
    e.update(TOUCH)
    e.update(PLAINBOX)
    e.update(w32(0x0011B0BA, PLAIN_TABLE))
    e.update(b8(0x00FFFED9, x))
    e.update(b8(0x00FFFEDA, y))
    e.update(b8(0x0011A17C, 0x00))
    e.update(w16(0x00114DE0, 0x1234))
    e.update(w16(0x0011A17E, 0xFFFE))
    e['11B360:8']='00'          # the display test's own step counter
    # An item whose category draws its preview at fixed coordinates: the
    # ones that scale a picture into the panel put a tall picture's header
    # outside the buffer.
    e.update(w16(0x00FFFEE0, 0x0004))
    for r in range(0x31):
        e.update(w32(0x0E9000 + 0x18*r + 0x04, 0x0034C6CD))
        e.update(w32(0x0E9000 + 0x18*r + 0x08, 0x0034C6CD))
        e.update(w32(0x0E9000 + 0x18*r + 0x0C, 0x0034C6CD))
        # Two of the four the menu list names carry categories of their own,
        # so that two handlers asking for different ones answer differently.
        e.update(b8(0x0E9000 + 0x18*r + 0x17,
                    0x02 if r == 0x0A else
                    0x07 if r == 0x02 else
                    0x06 if r == 0x03 else 0x12))
    if extra: e.update(extra)
    dispatch_case(name, screen, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_%02X (%s)'%(screen, name)

for nm, sc, at in (('the calibration screen', 0x00, 0x00115CDE),
                   ('the six-category menu', 0x05, 0x0011705E),
                   ('the ten-category menu', 0x06, 0x00117146),
                   ('the three-category menu', 0x25, 0x0011720A),
                   ('the five-category menu', 0x26, 0x0011721A)):
    plain_case('just arrived', sc, 1, 0, [at])
    plain_case('laid out again', sc, 0, 1, [at])
    plain_case('a plain pass', sc, 0, 0, [at])
    plain_case('a press on the first box', sc, 0, 0, [at], x=0x0C, y=0x24)

# The tables the display bring-up fills in, which two of these screens take
# their picture and their list from.
SLOTS = collections.OrderedDict()
SLOTS['0E9820:20']='00'
SLOTS.update(w32(0x0011B2B6, 0x000E9820))
SLOTS.update(w32(0x0E9824, 0x000E9800))       # +4: screen H'0E's picture
SLOTS.update(w32(0x0E9828, 0x000E9840))       # +8: screen H'0F's, its own
SLOTS.update(w16(0x0E9840, 0x0002))           # a picture of one run
SLOTS.update(w16(0x0E9842, 0x0866))
SLOTS.update(w32(0x0011B2BE, 0x0011A88E))   # and the list H'0F fills from

for nm, sc, at in (('the four-screen menu', 0x0E, 0x0011609C),
                   ('the twelve-box list', 0x0F, 0x0011658E)):
    for w, arrived, relayout in (('just arrived', 1, 0),
                                 ('a plain pass', 0, 0)):
        plain_case(w, sc, arrived, relayout, [at], extra=SLOTS)
    plain_case('a press on the first box', sc, 0, 0, [at], x=0x0C, y=0x24,
               extra=SLOTS)
plain_case('just arrived, the other stitch set', 0x0E, 1, 0, [0x0011609C],
           extra=dict(list(SLOTS.items()) + list(b8(0x0057FF80, 0xAA).items())))

# The picture table the four "pick one" screens take theirs from, with a
# different picture at each offset so that one cannot be mistaken for
# another.
PICKS = collections.OrderedDict(SLOTS)
PICKS['0E9860:40']='00'
PICKS.update(w32(0x0011B2A6, 0x000E9860))
for k, off in enumerate((0x04, 0x08, 0x0C, 0x10)):
    at = 0x0E98A0 + 0x10*k
    PICKS.update(w32(0x0E9860 + off, at))
    PICKS.update(w16(at + 0x00, 0x0002))
    PICKS.update(w16(at + 0x02, (0x04 + k) * 0x100 + 0x50 + k))

for nm, sc, at in (('the first pick screen',  0x39, 0x00116184),
                   ('the second pick screen', 0x3B, 0x001162B2),
                   ('the third pick screen',  0x3C, 0x0011639A),
                   ('the fourth pick screen', 0x3D, 0x0011644C)):
    plain_case('just arrived', sc, 1, 0, [at], extra=PICKS)
    plain_case('a plain pass', sc, 0, 0, [at], extra=PICKS)
    plain_case('a press on the first box', sc, 0, 0, [at], x=0x0C, y=0x24,
               extra=PICKS)
plain_case('just arrived, the other stitch set', 0x39, 1, 0, [0x00116184],
           extra=dict(list(PICKS.items()) + list(b8(0x0057FF80, 0xAA).items())))

for w, arrived in (('just arrived', 1), ('a plain pass', 0)):
    plain_case(w, 0x19, arrived, 0, [0x00118280], extra=PICKS)
    plain_case(w, 0x1A, arrived, 0, [0x00118280], extra=PICKS)
    plain_case(w, 0x1B, arrived, 0, [0x00118280], extra=PICKS)
plain_case('just arrived, the other stitch set', 0x19, 1, 0, [0x00118280],
           extra=dict(list(PICKS.items()) + list(b8(0x0057FF80, 0xAA).items())))
plain_case('a press on the first box', 0x1A, 0, 0, [0x00118280], x=0x0C, y=0x24,
           extra=PICKS)

# The two other picture tables these screens reach through.
PICKS.update(w32(0x0011B2AE, 0x000E9860))
PICKS.update(w32(0x0011B2A2, 0x000E9860))
PICKS.update(w32(0x0E9860 + 0x0C, 0x000E98D0))
PICKS.update(w16(0x0E98D0, 0x0002))
PICKS.update(w16(0x0E98D2, 0x0677))
PICKS.update(w32(0x0E9860 + 0x50, 0x0034C6CD))   # screen H'48's

PICKS.update(w32(0x0011B2C6, 0x000E9900))
PICKS['0E9900:20']='00'
PICKS.update(w32(0x0E9900 + 4*3, 0x0034C6CD))   # H'11B0FE = 3 picks this one
PICKS.update(b8(0x0011B0FE, 0x03))
# Three different pictures, so that one screen's cannot pass for another's.
PICKS.update(w32(0x0E9860 + 0x48, 0x00343D40))
PICKS.update(w32(0x0E9860 + 0x4C, 0x0034BA1B))

for sc, at in ((0x10, 0x00116688), (0x4C, 0x00118D1E)):
    plain_case('just arrived', sc, 1, 0, [at], extra=PICKS)
    plain_case('a plain pass', sc, 0, 0, [at], extra=PICKS)
plain_case('just arrived, the other stitch set', 0x10, 1, 0, [0x00116688],
           extra=dict(list(PICKS.items()) + list(b8(0x0057FF80, 0xAA).items())))
plain_case('just arrived, no picture for it', 0x10, 1, 0, [0x00116688],
           extra=dict(list(PICKS.items()) + list(b8(0x0011B0FE, 0x05).items())))
for w, arrived in (('just arrived', 1), ('a plain pass', 0)):
    for bit, nm in ((0x01, 'the module there'), (0x00, 'no module')):
        plain_case('%s, %s'%(w, nm), 0x49, arrived, 0, [0x00118D0E],
                   extra=dict(list(PICKS.items()) +
                              list(b8(0x00FFFEC4, bit).items())))

# H'11B2B2 gets a table of its own: screen H'28 blits what it finds there,
# so it has to be a bitmap and not the run list the others unpack.
PICKS['0E9940:20']='00'
PICKS.update(w32(0x0011B2B2, 0x000E9940))
PICKS.update(w32(0x0E9940 + 0x04, 0x0034C6CD))
for sc, at in ((0x28, 0x001185A0), (0x29, 0x001186AC),
               (0x2A, 0x00118728), (0x2C, 0x0011880C)):
    plain_case('just arrived', sc, 1, 0, [at], extra=PICKS)
    plain_case('a plain pass', sc, 0, 0, [at], extra=PICKS)
    plain_case('a press on the first box', sc, 0, 0, [at], x=0x0C, y=0x24,
               extra=PICKS)

PICKS.update(w32(0x0E9940 + 0x08, 0x0034BA1B))
PICKS.update(w32(0x0E9940 + 0x0C, 0x00343D40))
for sc, at in ((0x2F, 0x00118B32), (0x01, 0x00118B9C), (0x32, 0x00117272)):
    plain_case('just arrived', sc, 1, 0, [at], extra=PICKS)
    plain_case('a plain pass', sc, 0, 0, [at], extra=PICKS)
    plain_case('a press on the first box', sc, 0, 0, [at], x=0x0C, y=0x24,
               extra=PICKS)
for sc, at in ((0x2F, 0x00118B32), (0x32, 0x00117272)):
    plain_case('just arrived, the other stitch set', sc, 1, 0, [at],
               extra=dict(list(PICKS.items()) + list(b8(0x0057FF80, 0xAA).items())))

# The two alphabet lists the four screens fill from.
PICKS.update(w32(0x0011B09A, 0x0011A88E))
PICKS.update(w32(0x0011B09E, 0x0011A88E))
for sc in (0x33, 0x34, 0x35, 0x36):
    plain_case('just arrived', sc, 1, 0, [0x0011749E], extra=PICKS)
    plain_case('laid out again', sc, 0, 1, [0x0011749E], extra=PICKS)
    plain_case('a plain pass', sc, 0, 0, [0x0011749E], extra=PICKS)
plain_case('just arrived, with the queue strip', 0x35, 1, 0, [0x0011749E],
           extra=dict(list(PICKS.items()) + list(b8(0x0011A174, 0x01).items())))
plain_case('just arrived, with the queue strip', 0x33, 1, 0, [0x0011749E],
           extra=dict(list(PICKS.items()) + list(b8(0x0011A174, 0x01).items())))

# ---------------------------------------------------------------- the leaves
for n, (dest, value, count) in enumerate(
        ((0x000E5800, 0x5A, 0x00000010),
         (0x000E5800, 0x00, 0x00000000),
         (0x000E5800, 0xFF, 0x00000001),
         (0x000E5800, 0x33, 0x00000200))):
    f=base_fill({'0E5800:400':'00'})
    add('mem_set_long (%d bytes of %02X)'%(count, value), f,
        {'addr':'20076C','regs':{'er6':'%X'%dest,'er5':'%X'%count,'er4':'%X'%value}},
        {'symbol':'_mem_set_long','regs':{'er0':'%X'%dest,'er1':'%X'%value,'er2':'%X'%count}})

def needle_case(name, x, y, to=0xFFFF, extra=None):
    e = collections.OrderedDict()
    e['0E9000:8000']='00'
    e.update(block(0x00115F4A))
    e.update(TOUCH); e.update(PLAINBOX)
    e.update(w32(0x0011B0BA, PLAIN_TABLE))
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    e.update(b8(0x0011A17C, 0x00))
    e.update(w16(0x00114DE0, 0x1234))
    e.update(w16(0x0011A17E, 0xFFFE))
    e.update({'11B10E:1': '%02X'%(to >> 8), '11B10F:1': '%02X'%(to & 0xFF)})
    e.update(b8(0x0011B0A7, 0x27))
    e.update(b8(0x00FFFEFA, 0x00))
    e['11B360:8']='00'
    for r in range(0x31):
        e.update(w32(0x0E9000 + 0x18*r + 0x04, 0x0034C6CD))
        e.update(w32(0x0E9000 + 0x18*r + 0x08, 0x0034C6CD))
        e.update(w32(0x0E9000 + 0x18*r + 0x0C, 0x0034C6CD))
        e.update(b8(0x0E9000 + 0x18*r + 0x17, 0x02 if r == 0x0A else 0x12))
    if extra: e.update(extra)
    f=base_fill(e)
    add('needle_choice_screen (%s)'%name, f,
        {'addr':'21935E','result':'r6l'},
        {'symbol':'_needle_choice_screen','result':'r0l'})

# Box one carries value H'83 and the rest H'84 up, so a press on box n picks
# the n'th needle position.
NEEDLE = collections.OrderedDict()
for i in range(1, 0x0A):
    NEEDLE.update(w16(PLAIN_TABLE + 0x12*i + 0x08, 0x0082 + i))
needle_case('nothing pressed', 0x00, 0x00)
for i in range(1, 6):
    needle_case('the %d position box' % i, 0x0C + 0x10*(i-1), 0x24, extra=NEEDLE)
needle_case('the way back', 0x0C, 0x24,
            extra=dict(list(NEEDLE.items()) +
                       list(w16(PLAIN_TABLE + 0x12 + 0x08, 0x001A).items())))
needle_case('the way back, held off', 0x0C, 0x24,
            extra=dict(list(NEEDLE.items()) +
                       list(w16(PLAIN_TABLE + 0x12 + 0x08, 0x001A).items()) +
                       list(b8(0x00114DC6, 0x80).items())))
needle_case('the key that remembers', 0x0C, 0x24,
            extra=dict(list(NEEDLE.items()) +
                       list(w16(PLAIN_TABLE + 0x12 + 0x08, 0x000B).items())))
needle_case('a key the panel takes', 0x0C, 0x24,
            extra=dict(list(NEEDLE.items()) +
                       list(w16(PLAIN_TABLE + 0x12 + 0x08, 0x0008).items())))
needle_case('the other key the panel takes', 0x0C, 0x24,
            extra=dict(list(NEEDLE.items()) +
                       list(w16(PLAIN_TABLE + 0x12 + 0x08, 0x000C).items())))
# Bit three of H'FFFEFA up, so that a mask of H'F8 can be told from H'F0.
needle_case('the first position box, another bit up', 0x0C, 0x24,
            extra=dict(list(NEEDLE.items()) + list(b8(0x00FFFEFA, 0x08).items())))
# Box eight carries H'88, one past the five positions, and box nine a value
# only a search that reaches it can find.
needle_case('a box past the last position', 0x0C + 0x10*7, 0x24,
            extra=dict(list(NEEDLE.items()) +
                       list(w16(PLAIN_TABLE + 0x12*8 + 0x08, 0x0088).items())))
needle_case('the ninth box', 0x0C + 0x10*8, 0x24,
            extra=dict(list(NEEDLE.items()) +
                       list(w16(PLAIN_TABLE + 0x12*9 + 0x08, 0x0085).items())))
needle_case('a value nothing claims', 0x0C, 0x24,
            extra=dict(list(NEEDLE.items()) +
                       list(w16(PLAIN_TABLE + 0x12 + 0x08, 0x0055).items())))

PICKS.update(w32(0x0E9860 + 0x58, 0x0034BAE7))
def slot2_case(name, x, y, extra=None):
    e = collections.OrderedDict()
    e['0E9000:8000']='00'
    e.update(block(0x00118D2E))
    e.update(TOUCH); e.update(PLAINBOX)
    e.update(w32(0x0011B0BA, PLAIN_TABLE))
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    e.update(b8(0x0011A17C, 0x00))
    e.update(w16(0x00114DE0, 0x1234))
    e.update(w16(0x0011A17E, 0xFFFE))
    e.update(b8(0x0011B11A, 0x41))
    e.update(b8(0x0011A184, 0x01))
    e['11B360:8']='00'
    e['11A180:20']='00'
    if extra: e.update(extra)
    add('screen_slot_two_screen (%s)'%name, base_fill(e),
        {'addr':'21C592'}, {'symbol':'_screen_slot_two_screen'})

slot2_case('nothing pressed', 0x00, 0x00)
slot2_case('the first box', 0x0C, 0x24)
slot2_case('the second box', 0x1C, 0x24,
           extra=w16(PLAIN_TABLE + 0x12*2 + 0x08, 0x0002))
slot2_case('the first box, leaving for another screen', 0x0C, 0x24,
           extra=b8(0x0011B11A, 0x12))
slot2_case('a box carrying something else', 0x0C, 0x24,
           extra=w16(PLAIN_TABLE + 0x12 + 0x08, 0x0055))

for sc, at in ((0x4B, 0x00118CB8), (0x4D, 0x00118D2E)):
    plain_case('just arrived', sc, 1, 0, [at], extra=PICKS)
    plain_case('a plain pass', sc, 0, 0, [at], extra=PICKS)

for sc, at in ((0x0C, 0x00115F4A), (0x1C, 0x00118206), (0x27, 0x001184CC)):
    plain_case('just arrived', sc, 1, 0, [at], extra=PICKS)
    plain_case('a plain pass', sc, 0, 0, [at], extra=PICKS)
plain_case('just arrived, the other stitch set', 0x27, 1, 0, [0x001184CC],
           extra=dict(list(PICKS.items()) + list(b8(0x0057FF80, 0xAA).items())))

for sc, at in ((0x1D, 0x0011823A), (0x1E, 0x00118280), (0x20, 0x001183C0),
               (0x22, 0x00118280), (0x4A, 0x00118536), (0x48, 0x00118CFE)):
    plain_case('just arrived', sc, 1, 0, [at], extra=PICKS)
    plain_case('a plain pass', sc, 0, 0, [at], extra=PICKS)

for nm, sc, at in (('the message screen', 0x3E, 0x00115D12),
                   ('the picture choice', 0x0D, 0x00116032)):
    plain_case('just arrived', sc, 1, 0, [at])
    plain_case('laid out again', sc, 0, 1, [at])
    plain_case('a plain pass', sc, 0, 0, [at])
    plain_case('a press on the first box', sc, 0, 0, [at], x=0x0C, y=0x24)
plain_case('the message screen, held off', 0x3E, 1, 0, [0x00115D12],
           extra=b8(0x0011A17D, 0x01))

# H'0B draws nothing at all: it reads two bits of H'FFFEFA and leaves.
for bits, nm in ((0x20, 'the first setting'), (0x40, 'the second'),
                 (0x60, 'both bits up'), (0x00, 'neither')):
    plain_case(nm, 0x0B, 1, 0, [0x00115CDE], extra=b8(0x00FFFEFA, bits))
plain_case('nothing to do', 0x0B, 0, 0, [0x00115CDE])

# ------------------------------------------------- the stitch-size screen
# The font H'21D55C draws with is at H'119A66. Rather than invent one, the
# table the text_draw cases already build at H'0E1000 is copied there, glyph
# data and all -- only the table itself moves, so the pointers inside it
# still point at the glyphs where they are.
FONT = collections.OrderedDict()
_td = [c for c in cases if c['name'] == 'text_draw (left)'][0]['fill']
FONT['119A66:400']='00'
for k, v in _td.items():
    a, span = k.split(':')
    at = int(a, 16)
    if 0x0E1000 <= at < 0x0E1400:
        FONT['%06X:%s' % (0x00119A66 + (at - 0x0E1000), span)] = v
    elif 0x0E1500 <= at < 0x0E1700:
        FONT[k] = v

def size_extra(extra=None):
    e = collections.OrderedDict()
    e['0E9000:8000']='00'
    e.update(block(0x00118DCE))
    e.update(block(0x00118E4A))
    e.update(FONT)
    e.update(RUNLIST)
    e.update(TOUCH); e.update(PLAINBOX)
    e.update(w32(0x0011B0BA, PLAIN_TABLE))
    e.update(b8(0x0011A17C, 0x00))
    e.update(w16(0x00114DE0, 0x1234))
    e.update(w16(0x0011A17E, 0xFFFE))
    e['0E8010:400']='77'        # the scratch buffer, so that a picture that
                                # never reached it can be told from one that did
    e['11B380:10']='00'
    e['11BBAA:100']='00'
    # Distinct bytes in the record the queue position names, so that one
    # field cannot be mistaken for its neighbour.
    for off, val in ((4, 0x44), (5, 0x11), (6, 0x22), (7, 0x33)):
        e.update(b8(0x0011BBAA + 13*2 + off, val))
    e.update(w16(0x00FFFEFE, 0x0002))
    e.update(w16(0x00FFFEE0, 0x0004))
    e.update(b8(0x00FFFEFB, 0x05))
    e.update(b8(0x00FFFEFC, 0xFB))
    e.update(b8(0x00FFFEFD, 0x00))
    e.update(b8(0x00FFFEE2, 0x00))
    e.update(b8(0x0011A175, 0x00))
    e['11B360:8']='00'
    e['11A180:20']='00'
    for r in range(0x31):
        e.update(w32(0x0E9000 + 0x18*r + 0x04, 0x0034C6CD))
        e.update(w32(0x0E9000 + 0x18*r + 0x08, 0x0034C6CD))
        e.update(w32(0x0E9000 + 0x18*r + 0x0C,
                     (0x00343D40, 0x0034BA1B, 0x0034BAE7, 0x0034C6CD)[r % 4]))
        # A different picture at each of the two pointers, and a different
        # one per record, so that neither the field nor the record can be
        # mistaken for its neighbour.
        e.update(w32(0x0E9000 + 0x18*r + 0x10,
                     (0x0034C6CD, 0x00343D40, 0x0034BA1B, 0x0034BAE7)[r % 4]))
        e.update(b8(0x0E9000 + 0x18*r + 0x17, 0x02 if r == 0x0A else 0x12))
    if extra: e.update(extra)
    return e

def size_fill(extra=None):
    c = {'fill': base_fill(size_extra(extra))}
    move_wipe(c)
    return c['fill']

for off, nm in ((5, 'byte five'), (6, 'byte six')):
    for rec in (0x0002, 0x0000):
        add('queue_get_%s (record %d)'%(nm.replace(' ','_'), rec),
            size_fill(),
            {'addr':'228EBE' if off == 5 else '228EFA','result':'r6l',
             'regs':{'er6':'%04X'%rec}},
            {'symbol':'_queue_get_byte%d'%off,'result':'r0l',
             'regs':{'er0':'%04X'%rec}})

for v, which in ((0x05, 1), (0xFB, 1), (0x05, 2), (0xFB, 2),
                 (0x05, 3), (0xFB, 3), (0x00, 1), (0x05, 4)):
    add('offset_number_draw (%02X in place %d)'%(v, which), size_fill(),
        {'addr':'21D55C','regs':{'er6':'%02X'%v},'stack':{'4':'2:%04X'%which}},
        {'symbol':'_offset_number_draw','regs':{'er0':'%02X'%v,'er1':'%02X'%which}})

def size_case(name, fresh, x=0x00, y=0x00, extra=None):
    e = collections.OrderedDict()
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    if extra: e.update(extra)
    add('stitch_size_screen (%s)'%name, size_fill(e),
        {'addr':'21D88A','regs':{'er6':'%02X'%fresh}},
        {'symbol':'_stitch_size_screen','regs':{'er0':'%02X'%fresh}})

# Box one carries each key in turn.
def key(v):
    return w16(PLAIN_TABLE + 0x12 + 0x08, v)

size_case('the first pass, one number', 0x01)
size_case('the first pass, a group', 0x01, extra=b8(0x0011B0AC, 0x01))
size_case('the first pass, from the queue', 0x01,
          extra=dict(list(b8(0x0011A175, 0x01).items()) +
                     list(b8(0x0011B0AC, 0x01).items())))
size_case('nothing pressed', 0x00)
for v, nm in ((0x0018, 'the first number down'), (0x0017, 'the first number up'),
              (0x0015, 'the second number down'), (0x0016, 'the second number up'),
              (0x007F, 'both back to zero'), (0x0019, 'leaving with the change'),
              (0x001A, 'leaving without it'), (0x0055, 'a key nothing claims')):
    size_case(nm, 0x00, 0x0C, 0x24,
              extra=dict(list(key(v).items()) +
                         list(b8(0x0011B0AC, 0x01).items()) +
                         list(b8(0x0011B388, 0x05).items()) +
                         list(b8(0x0011B389, 0xFB).items()) +
                         list(b8(0x0011B386, 0x11).items()) +
                         list(b8(0x0011B387, 0x22).items())))
    size_case(nm + ', with the queue', 0x00, 0x0C, 0x24,
              extra=dict(list(key(v).items()) +
                         list(b8(0x0011A175, 0x01).items()) +
                         list(b8(0x0011B0AC, 0x01).items()) +
                         list(b8(0x0011B388, 0x05).items()) +
                         list(b8(0x0011B389, 0xFB).items()) +
                         list(b8(0x0011B386, 0x11).items()) +
                         list(b8(0x0011B387, 0x22).items())))
# Boxes six and seven are only in range when the pattern is a group, which
# is what tells the two hit-test ranges apart.
for box, nm in ((6, 'the sixth box'), (7, 'the seventh box')):
    for grp, how in ((0x01, 'a group'), (0x00, 'one number')):
        size_case('%s, %s'%(nm, how), 0x00, 0x0C + 0x10*(box-1), 0x24,
                  extra=dict(list(w16(PLAIN_TABLE + 0x12*box + 0x08, 0x0018).items()) +
                             list(b8(0x0011B0AC, grp).items()) +
                             list(b8(0x0011B388, 0x05).items())))

size_case('the first number already at its floor', 0x00, 0x0C, 0x24,
          extra=dict(list(key(0x0018).items()) +
                     list(b8(0x0011B388, 0xCE).items())))
size_case('the first number already at its ceiling', 0x00, 0x0C, 0x24,
          extra=dict(list(key(0x0017).items()) +
                     list(b8(0x0011B388, 0x32).items())))
size_case('the second number already at its floor', 0x00, 0x0C, 0x24,
          extra=dict(list(key(0x0015).items()) +
                     list(b8(0x0011B0AC, 0x01).items()) +
                     list(b8(0x0011B389, 0xEC).items())))
size_case('the second number already at its ceiling', 0x00, 0x0C, 0x24,
          extra=dict(list(key(0x0016).items()) +
                     list(b8(0x0011B0AC, 0x01).items()) +
                     list(b8(0x0011B389, 0x14).items())))

for nm, extra in (('just arrived', None),
                  ('just arrived, a group', b8(0x00FFFEE2, 0x04)),
                  ('just arrived, from the queue',
                   dict(list(b8(0x0011A175, 0x01).items()) +
                        list(b8(0x00FFFEE2, 0x04).items()))),
                  ('just arrived, from the queue, one number',
                   b8(0x0011A175, 0x01)),
                  ('just arrived, the position moved on',
                   dict(list(b8(0x00FFFEFD, 0x03).items()) +
                        list(b8(0x00FFFEE2, 0x04).items()))),
                  ('a plain pass', None)):
    e = collections.OrderedDict()
    if extra: e.update(extra)
    dispatch_case(nm, 0x3F, 0 if nm == 'a plain pass' else 1, 0, 0,
                  extra=size_extra(e))
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_3F (%s)'%nm

# ------------------------------------------------- the stitch-length screen
def len_extra(extra=None):
    e = size_extra()
    e.pop('118DCE:10', None); e.pop('118E4A:10', None)
    e.update(block(0x00115E80))
    e['0E0010:4']='00'
    e.update(b8(0x000E0010, 0x03))     # the block bytes the length hangs off
    e.update(b8(0x000E0011, 0x0C))
    e.update(b8(0x0011A7AC, 0x28))
    e['11B320:8']='00'
    e.update(b8(0x0011B321, 0x05))
    e.update(b8(0x00114DC6, 0x00))
    e.update(b8(0x00FFFEF7, 0xFF))     # so that clearing one bit can be seen
    e['250760:10']='00'
    # The unit the number is appended with. Left as "mm" the font the cases
    # build has no glyph for it, and text_draw walks off with a null glyph
    # pointer whose header claims a thousand rows.
    e['250AE0:4']='00'
    if extra: e.update(extra)
    return e

def len_fill(extra=None):
    c = {'fill': base_fill(len_extra(extra))}
    move_wipe(c)
    return c['fill']

for v in (0x28, 0x00, 0xFF, 0x0F, 0x0A, 0x0B):
    add('stitch_length_shown (H\'%02X)'%v, len_fill(b8(0x0011A7AC, v)),
        {'addr':'20669A','result':'r6l'},
        {'symbol':'_stitch_length_shown','result':'r0l'})
    add('stitch_length_choose (H\'%02X)'%v, len_fill(),
        {'addr':'2066CC','regs':{'er6':'%02X'%v}},
        {'symbol':'_stitch_length_choose','regs':{'er0':'%02X'%v}})

def len_case(name, fresh, x=0x00, y=0x00, extra=None):
    e = collections.OrderedDict()
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    if extra: e.update(extra)
    add('stitch_length_screen (%s)'%name, len_fill(e),
        {'addr':'218FCE','regs':{'er6':'%02X'%fresh},'result':'r6l'},
        {'symbol':'_stitch_length_screen','regs':{'er0':'%02X'%fresh},
         'result':'r0l'})

len_case('the first pass', 0x01)
len_case('a plain pass', 0x00)
len_case('a plain pass, the wedge already up', 0x00,
         extra=dict(list(w16(0x0011B322, 0x0020).items()) +
                    list(w16(0x0011B320, 0x0004).items()) +
                    list(b8(0x0011B101, 0x30).items()) +
                    list(w16(0x0011B326, 0x0040).items())))
len_case('a plain pass, the wedge to come down', 0x00,
         extra=dict(list(w16(0x0011B322, 0x0030).items()) +
                    list(w16(0x0011B320, 0x0004).items()) +
                    list(b8(0x0011B101, 0x05).items()) +
                    list(w16(0x0011B326, 0x0040).items())))
len_case('a plain pass, over the limit', 0x00,
         extra=dict(list(b8(0x0011B101, 0x64).items()) +
                    list(w16(0x0011B326, 0x0002).items()) +
                    list(w16(0x0011B320, 0x0007).items()) +
                    list(b8(0x0011B324, 0x11).items())))
len_case('a plain pass, past a hundred', 0x00, extra=b8(0x0011B101, 0xC8))
len_case('a plain pass, one past the hundred', 0x00,
         extra=dict(list(b8(0x0011B101, 0x65).items()) +
                    list(w16(0x0011B326, 0x0040).items())))
# The wedge exactly at the limit, which is where the test that keeps it
# there changes hands.
len_case('a plain pass, exactly at the limit', 0x00,
         extra=dict(list(b8(0x0011B101, 0x30).items()) +
                    list(w16(0x0011B326, 0x000F).items()) +
                    list(w16(0x0011B320, 0x0004).items()) +
                    list(b8(0x0011B324, 0x11).items())))
len_case('the third box', 0x00, 0x0C + 0x20, 0x24,
         extra=dict(list(w16(PLAIN_TABLE + 0x12*3 + 0x08, 0x0019).items()) +
                    list(w16(0x0011B326, 0x0040).items())))
for v, nm in ((0x0019, 'the key that keeps it'), (0x001A, 'the key that does not'),
              (0x0055, 'a key nothing claims')):
    len_case(nm, 0x00, 0x0C, 0x24,
             extra=dict(list(w16(PLAIN_TABLE + 0x12 + 0x08, v).items()) +
                        list(w16(0x0011B326, 0x0040).items())))

for nm, arrived in (('just arrived', 1), ('a plain pass', 0)):
    dispatch_case(nm, 0x0A, arrived, 0, 0, extra=len_extra())
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_0A (%s)'%nm

# ------------------------------------------------- the module version screen
def ver_extra(extra=None):
    e = size_extra()
    e.pop('118DCE:10', None); e.pop('118E4A:10', None)
    e['104C90:20']='00'
    # Digits only: the font these cases build has no others, and text_draw
    # takes a null glyph pointer a long way outside the buffer.
    for k, ch in enumerate('102'):
        e.update(b8(0x00104C90 + k, ord(ch)))
    e.update(w16(0x0011B10E, 0xFFFF))
    e.update(b8(0x0011A17C, 0x00))
    e.update(w16(0x0011A17E, 0xFFFE))
    if extra: e.update(extra)
    return e

def ver_fill(extra=None):
    c = {'fill': base_fill(ver_extra(extra))}
    move_wipe(c)
    return c['fill']

add('module_version_text_draw', ver_fill(),
    {'addr':'248EEE'}, {'symbol':'_module_version_text_draw'})
for fresh in (0x01, 0x00):
    for to, nm in ((0xFFFF, 'nothing asked for'), (0x0077, 'the way out'),
                   (0x0031, 'somewhere else')):
        pass
# The screen it is already on, which is the only thing the forced flag
# changes.
add('module_version_press (already on H\'77)',
    ver_fill(dict(list(w16(0x0011B10E, 0x0077).items()) +
                  list(w16(0x0011A17E, 0x0077).items()))),
    {'addr':'230E2C','regs':{'er6':'00'},'result':'r6l'},
    {'symbol':'_module_version_press','regs':{'er0':'00'},'result':'r0l'})
for fresh in (0x01, 0x00):
    for to, nm in ((0xFFFF, 'nothing asked for'), (0x0077, 'the way out'),
                   (0x0031, 'somewhere else')):
        add('module_version_press (%s, %s)'%(
                'the first pass' if fresh else 'a plain pass', nm),
            ver_fill(w16(0x0011B10E, to)),
            {'addr':'230E2C','regs':{'er6':'%02X'%fresh},'result':'r6l'},
            {'symbol':'_module_version_press','regs':{'er0':'%02X'%fresh},
             'result':'r0l'})

for nm, arrived in (('just arrived', 1), ('a plain pass', 0)):
    dispatch_case(nm, 0x21, arrived, 0, 0, extra=ver_extra())
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_21 (%s)'%nm

# ------------------------------------------------- the panel's marks
def marks_fill(shown, wanted, extra=None):
    e = size_extra()
    e.pop('118DCE:10', None); e.pop('118E4A:10', None)
    e['0E6800:80']='00'
    # The panel bits the keys in these lists switch, up so that clearing
    # one can be seen.
    e.update(b8(0x00FFFEF5, 0xFF))
    e.update(b8(0x00FFFEF6, 0xFF))
    e.update(b8(0x00FFFEF9, 0xFF))
    e.update(w16(0x000E6800, len(shown)))
    for k, v in enumerate(shown):
        e.update(w16(0x000E6802 + 2*k, v))
    e.update(w16(0x000E6840, len(wanted)))
    for k, v in enumerate(wanted):
        e.update(w16(0x000E6842 + 2*k, v))
    if extra: e.update(extra)
    c = {'fill': base_fill(e)}
    move_wipe(c)
    return c['fill']

for shown, wanted, tell, nm in (
        ((0x0011, 0x0012, 0x0013), (0x0012,), 0x00, 'one of three wanted'),
        ((0x0002, 0x0003, 0x0005), (0x0003,), 0x01, 'and the rest told'),
        ((0x0011, 0x0012, 0x0013), (0x0011, 0x0012, 0x0013), 0x00, 'all of them'),
        ((0x0011, 0x0012, 0x0013), (), 0x00, 'none of them'),
        ((), (0x0012,), 0x00, 'nothing shown'),
        ((0x0002,), (0x0099,), 0x01, 'one, not wanted, told')):
    add('panel_marks_match (%s)'%nm, marks_fill(shown, wanted),
        {'addr':'21DC56','regs':{'er6':'E6800'},
         'stack':{'4':'4:000E6840','8':'2:%04X'%tell}},
        {'symbol':'_panel_marks_match','regs':{'er0':'E6800','er1':'E6840',
                                               'er2':'%02X'%tell}})

# ------------------------------------------------- the panel strip, screen H'2E
STRIP_LIST_0  = 0x0057EED6
STRIP_KEYS_B4 = (0x115B0E, 0x115B30, 0x115B4E, 0x115B70,
                 0x115B92, 0x115BAA, 0x115BC2)
STRIP_KEYS_AA = (0x115BE2, 0x115C02, 0x115C1E, 0x115C3E,
                 0x115C5E, 0x115C74, 0x115C8A)
STRIP_SHOWN   = (0x115A20, 0x115A42, 0x115A64, 0x115A86,
                 0x115AA8, 0x115ACA, 0x115AEC)

def counted(at, vals):
    d = w16(at, len(vals))
    for j, v in enumerate(vals):
        d.update(w16(at + 2 + 2*j, v))
    return d

# Where the press has to land for the hit test to answer box [b], given the
# grid PLAINBOX lays the boxes out on.
def boxxy(b):
    return (0x0C + 0x10*((b - 1) % 12), 0x24 + 0x10*((b - 1) // 12))

# Fifteen marks in each strip, which is as many as the H'22 bytes hold once
# one more has been put in front: a shorter one leaves the last two bytes of
# every copy and every flash write alike whatever the length is.
def strip_marks(s):
    return tuple(v + s for v in (0x10, 0x45 - s, 0x20, 0x30, 0x40, 0x50, 0x60,
                                 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x12, 0x22))

def marks_extra(strip=0, set_b4=True, extra=None):
    e = size_extra()
    e.pop('118DCE:10', None); e.pop('118E4A:10', None)
    e.update(block(0x00118AC8))                 # screen H'2E's own block
    # Every key these lists hold given a real picture in the icon table: the
    # boxes are filled from a list that is not one of the pattern ones, so
    # each value is looked up there and a pointer left over from the boot
    # would be blitted from.
    e['1158CE:200']='00'
    for k in range(0x80):
        e.update(w32(0x001158CE + 4*k, 0x0034C6CD))
    e['57EED6:EE']='00'         # the seven strips, in flash
    e['115A20:280']='00'        # what is shown, and the keys on offer
    for s in range(7):
        e.update(counted(STRIP_LIST_0 + 0x22*s, strip_marks(s)))
        e.update(counted(STRIP_SHOWN[s], (0x10+s, 0x20+s, 0x30+s, 0x40+s)))
        # H'02, H'03 and H'05 are keys the panel really switches, and none
        # of them is in a strip: they are what "tell the machine about the
        # ones that are not wanted" has to act on.
        e.update(counted(STRIP_KEYS_B4[s], (0x10+s, 0x45, 0x02, 0x03, 0x30+s)))
        e.update(counted(STRIP_KEYS_AA[s], (0x50+s, 0x45, 0x05, 0x02, 0x11+s)))
    e.update(w16(0x0057EFC4, 0x0012))           # the field, and its key
    e.update(b8(0x0057FF80, 0xB4 if set_b4 else 0xAA))
    e.update(w32(0x0011A196, STRIP_LIST_0 + 0x22*strip))
    e['11B386:40']='00'                         # the flags and the copy
    e.update(w32(0x0011B392, STRIP_KEYS_B4[strip]))
    e.update(w32(0x0011B38E, STRIP_SHOWN[strip]))
    # The working copy a press finds. H'13 is in it and in no box of the run
    # the search covers, so a search that ran past the end of that run would
    # answer a box rather than nothing.
    e.update(counted(0x0011B396, (0x0010, 0x0045, 0x0013)))
    e.update(b8(0x0011A18B, 0x00))              # an empty screen stack
    e.update(b8(0x00FFFEDB, 0x00))
    # The panel bits the keys switch, up so that clearing one can be seen.
    e.update(b8(0x00FFFEF5, 0xFF))
    e.update(b8(0x00FFFEF6, 0xFF))
    e.update(b8(0x00FFFEF9, 0xFF))
    if extra: e.update(extra)
    return e

def marks_fill2(strip=0, set_b4=True, extra=None):
    c = {'fill': base_fill(marks_extra(strip, set_b4, extra))}
    move_wipe(c)
    return c['fill']

for v, nm in ((0x0045, 'the one in the middle'), (0x0010, 'the first'),
              (0x0013, 'the last'), (0x0099, 'not there'),
              (0x0000, 'the count itself')):
    add('list_position (%s)'%nm, marks_fill2(),
        {'addr':'21EEC8','result':'r6','regs':{'er6':'11B396'},
         'stack':{'4':'2:%04X'%v}},
        {'symbol':'_list_position','result':'r0',
         'regs':{'er0':'11B396','er1':'%04X'%v}})
# A list whose count is also one of its values: counting from zero rather
# than from one would answer "not there" for it.
add('list_position (the count also a value)',
    marks_fill2(extra=counted(0x000E6900, (0x0010, 0x0003, 0x0020))),
    {'addr':'21EEC8','result':'r6','regs':{'er6':'E6900'},
     'stack':{'4':'2:0003'}},
    {'symbol':'_list_position','result':'r0',
     'regs':{'er0':'E6900','er1':'0003'}})

# The two that write all seven strips back to flash.
add('panel_strips_add_45', marks_fill2(),
    {'addr':'21EA60'}, {'symbol':'_panel_strips_add_45'})
add('panel_strips_drop_45', marks_fill2(),
    {'addr':'21EC80'}, {'symbol':'_panel_strips_drop_45'})
# One strip without H'45 in it at all, so that taking it out has nothing to
# take and the position of zero is what reaches the remover.
add('panel_strips_drop_45 (one strip without it)',
    marks_fill2(extra=counted(STRIP_LIST_0, (0x0010, 0x0020, 0x0030))),
    {'addr':'21EC80'}, {'symbol':'_panel_strips_drop_45'})

def marks_case(name, fresh, x=0x00, y=0x00, strip=0, set_b4=True, extra=None):
    e = collections.OrderedDict()
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    if extra: e.update(extra)
    add('panel_marks_screen (%s)'%name, marks_fill2(strip, set_b4, e),
        {'addr':'21E082','regs':{'er6':'%02X'%fresh},'result':'r6l'},
        {'symbol':'_panel_marks_screen','regs':{'er0':'%02X'%fresh},
         'result':'r0l'})

# The lay-out: each of the seven strips, and both data sets, so that the
# seven-way pick and the choice between the two lists of keys are both
# covered.
for s in range(7):
    marks_case('the first pass, strip %d'%s, 0x01, strip=s)
marks_case('the first pass, the other data set', 0x01, set_b4=False)
# A strip H'11A196 does not name: none of the seven tests matches and the
# two pointers stay as they were.
marks_case('the first pass, a strip of its own', 0x01,
           extra=w32(0x0011A196, 0x0057EEC0))
marks_case('the first pass, the field key held down', 0x01,
           extra=b8(0x00FFFEDB, 0x40))
marks_case('nothing pressed', 0x00)
marks_case('nothing pressed, the field key held down', 0x00,
           extra=b8(0x00FFFEDB, 0x40))

# The state a press finds: which box of each run is lit, what the lit one
# was before, and which run was touched last.
def lit(keybox=0x0000, stripbox=0x0000, was=0x00, run_key=0x00, run_strip=0x00,
        editing=0x00, added=0x00, dropped=0x00):
    d = collections.OrderedDict()
    d.update(w16(0x0011B38A, keybox))
    d.update(w16(0x0011B38C, stripbox))
    d.update(b8(0x0011B3BF, was))
    d.update(b8(0x0011B3BB, run_key))
    d.update(b8(0x0011B3BA, run_strip))
    d.update(b8(0x0011B3BE, editing))
    d.update(b8(0x0011B3BC, added))
    d.update(b8(0x0011B3BD, dropped))
    return d

def state(b, v):
    return b8(PLAIN_TABLE + 0x12*b + 0x10, v)

# One of the keys on offer, and one of the strip as it stands.
for b, nm in ((0x01, 'the first key'), (0x05, 'the fifth key'),
              (0x10, 'the sixteenth key'),
              (0x16, 'the first of the strip'),
              (0x18, 'the third of the strip')):
    x, y = boxxy(b)
    marks_case('%s pressed'%nm, 0x00, x, y,
               extra=dict(list(lit(0x0002, 0x0017, 0x05).items())))
    # The same box already lit, which is the one thing both bodies leave
    # alone.
    marks_case('%s pressed, already lit'%nm, 0x00, x, y,
               extra=dict(list(lit(0x0002, 0x0017, 0x05).items()) +
                          list(state(b, 0x01).items())))
# A key box in a state of its own, so that what the lit box is put back to
# is something other than nothing.
x, y = boxxy(0x03)
marks_case('a key box in a state of its own pressed', 0x00, x, y,
           extra=dict(list(lit(0x0002, 0x0017, 0x05).items()) +
                      list(state(0x03, 0x05).items())))

# Box H'11: back to the strip flash still holds.
x, y = boxxy(0x11)
marks_case('the field stepped on', 0x00, x, y, extra=lit(0x0002, 0x0017))
marks_case('the field stepped on, H\'45 already added', 0x00, x, y,
           extra=lit(0x0002, 0x0017, added=0x01))
marks_case('the field stepped on, no H\'45 in the copy', 0x00, x, y,
           extra=dict(list(lit(0x0002, 0x0017).items()) +
                      list(counted(0x0011B396, (0x0010, 0x0020)).items())))
marks_case('the field stepped on, the copy the longer', 0x00, x, y,
           extra=dict(list(lit(0x0002, 0x0017).items()) +
                      list(counted(0x0011B396,
                                   (0x10, 0x45, 0x20, 0x30, 0x40, 0x11)).items())))

# Box H'12: the key that is lit moved across.
x, y = boxxy(0x12)
marks_case('move across, nothing lit', 0x00, x, y, extra=lit())
marks_case('move across, into the field', 0x00, x, y,
           extra=lit(0x0001, 0x0017, 0x05, run_key=0x01, editing=0x01))
marks_case('move across, the key already in the strip', 0x00, x, y,
           extra=lit(0x0001, 0x0017, 0x05, run_key=0x01))
marks_case('move across, at the front', 0x00, x, y,
           extra=lit(0x0001, 0x0017, 0x00, run_key=0x01))
marks_case('move across, in front of the lit one', 0x00, x, y,
           extra=lit(0x0001, 0x0017, 0x00, run_key=0x01, run_strip=0x01))
marks_case('move across, H\'45 itself', 0x00, x, y,
           extra=lit(0x0002, 0x0017, 0x00, run_key=0x01, run_strip=0x01))
marks_case('move across, H\'45 itself, already dropped', 0x00, x, y,
           extra=lit(0x0002, 0x0017, 0x00, run_key=0x01, run_strip=0x01,
                     dropped=0x01))

# Box H'13: the lit box taken out.
x, y = boxxy(0x13)
marks_case('take out, nothing lit', 0x00, x, y, extra=lit())
marks_case('take out, the field emptied', 0x00, x, y,
           extra=lit(0x0001, 0x0017, editing=0x01))
marks_case('take out, the second of the strip', 0x00, x, y,
           extra=lit(0x0001, 0x0017, run_strip=0x01))
marks_case('take out, H\'45 itself', 0x00, x, y,
           extra=lit(0x0001, 0x0017, run_strip=0x01))
marks_case('take out, H\'45 itself, already added', 0x00, x, y,
           extra=lit(0x0001, 0x0017, run_strip=0x01, added=0x01))
marks_case('take out, the first of the strip', 0x00, x, y,
           extra=lit(0x0001, 0x0016, run_strip=0x01))
# The third of the strip carries H'13, which no box of the run the search
# covers holds but the boxes just past it do.
marks_case('take out, a key no box on offer carries', 0x00, x, y,
           extra=lit(0x0001, 0x0018, 0x05, run_strip=0x01))

# Boxes H'14 and H'15: accepted and cancelled.
x, y = boxxy(0x14)
marks_case('accepted', 0x00, x, y, extra=lit(0x0001, 0x0017))
marks_case('accepted, H\'45 added', 0x00, x, y,
           extra=lit(0x0001, 0x0017, added=0x01))
marks_case('accepted, H\'45 dropped', 0x00, x, y,
           extra=lit(0x0001, 0x0017, dropped=0x01))
marks_case('accepted, H\'45 both ways', 0x00, x, y,
           extra=lit(0x0001, 0x0017, added=0x01, dropped=0x01))
x, y = boxxy(0x15)
marks_case('cancelled', 0x00, x, y,
           extra=lit(0x0001, 0x0017, added=0x01, dropped=0x01))

for nm, arrived, ex in (('just arrived', 1, None),
                        ('a plain pass', 0, None),
                        ('a press on the first key', 0,
                         dict(list(b8(0x00FFFED9, boxxy(1)[0]).items()) +
                              list(b8(0x00FFFEDA, boxxy(1)[1]).items()) +
                              list(lit(0x0002, 0x0017, 0x05).items()))),
                        ('a press on the way out', 0,
                         dict(list(b8(0x00FFFED9, boxxy(0x15)[0]).items()) +
                              list(b8(0x00FFFEDA, boxxy(0x15)[1]).items()) +
                              list(lit(0x0001, 0x0017).items())))):
    dispatch_case(nm, 0x2E, arrived, 0, 0, extra=marks_extra(0, True, ex))
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_2E (%s)'%nm

# ------------------------------------------------- the two packed pictures
from lzwlib import stream_for

LZW_AT = 0x000E3000

def lzw_blob(at, data):
    d = collections.OrderedDict()
    i = 0
    while i < len(data):
        j = i
        while j < len(data) and data[j] == data[i]: j += 1
        d['%06X:%X'%(at+i, j-i)] = '%02X'%data[i]
        i = j
    return d

def px_for(w, h, kind='bands'):
    n = w * h
    if kind == 'bands':
        px = [(0, 3, 2, 0)[((k // w) + (k % w) // 8) % 4] for k in range(n)]
    elif kind == 'one':
        px = [0x03] * n
    else:
        px = [(0, 2, 3, 3, 0, 2, 2, 0)[k % 8] for k in range(n)]
    return px

def packed_extra(at, x0, y0, x1, y1, kind='bands', extra=None, picture=None):
    """A screen block whose picture is an LZW stream, and the stream.

    [picture] gives the stream's own size when it is not the block's: screen
    H'3A draws the whole screen whatever its block says. One wipe and one
    stream, because a second pair would land its bytes at the first pair's
    place in the fill and the second wipe would cover them."""
    e = size_extra()
    e.pop('118DCE:10', None); e.pop('118E4A:10', None)
    e.update(block(at, LZW_AT))
    e.update(w16(at + 0x04, x0)); e.update(w16(at + 0x06, y0))
    e.update(w16(at + 0x08, x1)); e.update(w16(at + 0x0A, y1))
    pw, ph = picture if picture else (x1 - x0 + 1, y1 - y0 + 1)
    stream = stream_for(px_for(pw, ph, kind), pw, ph)
    # Wiped to exactly the stream's length: H'6000 would reach the hit-box
    # table at H'0E5000 and H'1000 would leave the tail of a longer stream
    # from some other case behind.
    e['%06X:%X'%(LZW_AT, len(stream) + 0x10)]='00'
    e.update(lzw_blob(LZW_AT, stream))
    e.update(b8(0x0011A18B, 0x00))          # an empty screen stack
    e.update(b8(0x0011A179, 0x01))
    if extra: e.update(extra)
    return e

# Screen H'08. Its block is at H'115D12 and the rectangle is its own, so
# the same body covers the ragged ends by moving the block's corners.
for nm, x0, y0, x1, y1, kind in (
        ('just arrived',                0x0004, 0x0000, 0x0028, 0x0014, 'bands'),
        ('just arrived, ragged ends',    0x0005, 0x0002, 0x002A, 0x0010, 'bands'),
        ('just arrived, one value',      0x0004, 0x0000, 0x0027, 0x0014, 'one'),
        ('a plain pass',                 0x0004, 0x0000, 0x0028, 0x0014, 'bands')):
    dispatch_case(nm, 0x08, 0 if nm == 'a plain pass' else 1, 0, 0,
                  extra=packed_extra(0x00115D12, x0, y0, x1, y1, kind))
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_08 (%s)'%nm
# The way out the screen answers to: the link owner no longer waiting.
dispatch_case('the message let go', 0x08, 0, 0, 0,
              extra=packed_extra(0x00115D12, 0x0004, 0x0000, 0x0028, 0x0014,
                                 'bands', b8(0x00FFFEC4, 0x01)))
move_wipe(new[-1])
new[-1]['name'] = 'screen_body_08 (the message let go)'

# Screen H'3A draws the whole screen, which is the decoder's straight-run
# path and the only place the dictionary wraps in earnest.
for nm, kind in (('just arrived', 'bands'), ('a plain pass', 'bands')):
    # The picture is the whole screen whatever the block says: only the
    # rectangle copied out of the scratch buffer comes from the block.
    e = packed_extra(0x001161B8, 0x0000, 0x0000, 0x0028, 0x0014, kind,
                     picture=(0x0140, 0x00F0))
    dispatch_case(nm, 0x3A, 0 if nm == 'a plain pass' else 1, 0, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_3A (%s)'%nm

# ------------------------------------------------- the number keypad, H'46/H'47
# A second copy of the test font, at the address the keypad's text_draw is
# given as a constant.
FONT2 = collections.OrderedDict()
FONT2['11936E:400']='00'
for k, v in _td.items():
    a, span = k.split(':')
    at = int(a, 16)
    if 0x0E1000 <= at < 0x0E1400:
        FONT2['%06X:%s' % (0x0011936E + (at - 0x0E1000), span)] = v

KEYPAD_TABLE = 0x000E5000
LIST_B = 0x000E6800
LIST_C = 0x000E6840

def keypad_extra(extra=None):
    e = size_extra()
    e.pop('118DCE:10', None); e.pop('118E4A:10', None)
    e.update(block(0x00119352))
    e.update(FONT2)
    # Only the two words above the field: the shared fill sets the picker's
    # position and its two ends further up the same block, and wiping those
    # leaves the arrows with nothing to say.
    e['11A1BE:04']='00'
    e['11A88E:40']='00'
    e['0E6800:100']='00'
    e['11B3C0:20']='00'
    # Three lists of patterns, each with numbers of its own at offset H'14
    # of the descriptor, so that a number can only be in one of them.
    e.update(w16(0x0011A88E, 0x000C))
    for k, v in enumerate((0x0001, 0x0002, 0x0003, 0x0004, 0x000B, 0x000C,
                           0x000D, 0x000E, 0x000F, 0x0010, 0x0011, 0x0012)):
        e.update(w16(0x0011A890 + 2*k, v))
    e.update(w32(0x0011B09A, LIST_B))
    e.update(w16(LIST_B, 0x0003))
    for k, v in enumerate((0x0005, 0x0006, 0x0007)):
        e.update(w16(LIST_B + 2 + 2*k, v))
    e.update(w32(0x0011B09E, LIST_C))
    e.update(w16(LIST_C, 0x0003))
    for k, v in enumerate((0x0008, 0x0009, 0x000A)):
        e.update(w16(LIST_C + 2 + 2*k, v))
    # Every descriptor given a number and a category of its own.
    for r in range(0x31):
        e.update(w16(0x0E9000 + 0x18*r + 0x14, 0x0100 + r))
        e.update(b8(0x0E9000 + 0x18*r + 0x17,
                    0x12 if 0x0B <= r <= 0x12 else
                    (0x12, 0x10, 0x11, 0x04, 0x12, 0x13)[r % 6]))
    # Two of them carry the numbers the queue refuses.
    e.update(w16(0x0E9000 + 0x18*0x06 + 0x14, 0x0016))
    e.update(w16(0x0E9000 + 0x18*0x07 + 0x14, 0x0017))
    e.update(w32(0x000E6900, 0x11223344))
    e.update(b8(0x0011A18B, 0x02))        # something on the screen stack
    e.update(b8(0x0011A18C, 0x27))
    e.update(b8(0x0011A18D, 0x28))
    e.update(b8(0x0011B3CC, 0x5A))        # the two bytes the way out swaps
    e.update(b8(0x0011B3D6, 0x00))
    e.update(b8(0x0011B3D7, 0x00))
    e.update(b8(0x0011A169, 0x46))
    e.update(b8(0x0011A174, 0x00))
    e.update(b8(0x0011A178, 0x00))
    e.update(b8(0x0011B0A5, 0x02))
    e.update(w16(0x0011B118, 0x0007))
    e.update(b8(0x00FFFEFD, 0xA5))
    e.update(w16(0x00114DE0, 0x1234))
    if extra: e.update(extra)
    return e

def keypad_fill(extra=None):
    c = {'fill': base_fill(keypad_extra(extra))}
    move_wipe(c)
    return c['fill']

def field(s):
    d = collections.OrderedDict()
    # Eight bytes, not sixteen: H'11A1C2 plus H'10 reaches the picker's
    # position and its first, which the shared fill sets further up.
    d['11A1C2:08']='00'
    for k, ch in enumerate(s):
        d.update(b8(0x0011A1C2 + k, ord(ch)))
    return d

# H'212A44
for n, nm in ((0x0101, 'the first list'), (0x0106, 'the second'),
              (0x0109, 'the third'), (0x0100, 'the count word, not a number'),
              (0x0999, 'in none of them'), (0x0016, 'one of the odd two'),
              (0x0112, 'the last of the first list'),
              (0x0107, 'the last of the second'),
              (0x010A, 'the last of the third')):
    add('list_holding (%s)'%nm, keypad_fill(),
        {'addr':'212A44','result':'r6','regs':{'er6':'%04X'%n},
         'stack':{'4':'4:000E6900'}},
        {'symbol':'_list_holding','result':'r0',
         'regs':{'er0':'%04X'%n,'er1':'E6900'}})

# H'21A246
for n, nm in ((0x0101, 'the first list'), (0x0102, 'a category of H\'10'),
              (0x0103, 'a category of H\'11'), (0x0109, 'the third list'),
              (0x0999, 'in none of them'),
              (0x010D, 'three past the first of its category'),
              (0x010F, 'five past it'), (0x0110, 'six past it'),
              (0x0112, 'eight past it')):
    add('goto_pattern_number (%s)'%nm, keypad_fill(),
        {'addr':'21A246','result':'r6l','regs':{'er6':'%04X'%n}},
        {'symbol':'_goto_pattern_number','result':'r0l',
         'regs':{'er0':'%04X'%n}})

# H'24B10A. A string at H'0E3000, and the answer place at H'0E3100.
STR_AT = 0x000E3000
def strtol_case(text, base, nm):
    e = collections.OrderedDict()
    e['0E3000:120']='00'
    e['11F5A0:10']='00'
    for k, ch in enumerate(text):
        e.update(b8(STR_AT + k, ord(ch)))
    add('str_to_long (%s)'%nm, keypad_fill(e),
        {'addr':'24B10A','result':'er6','regs':{'er6':'%X'%STR_AT},
         'stack':{'4':'4:000E3100','8':'2:%04X'%(base & 0xFFFF)}},
        {'symbol':'_str_to_long','result':'er0',
         'regs':{'er0':'%X'%STR_AT,'er1':'E3100','er2':'%04X'%(base & 0xFFFF)}})

for text, base, nm in (
        ('123', 10, 'a plain number'),
        ('  \t 42', 10, 'led up to by space'),
        ('-17', 10, 'negative'),
        ('+17', 10, 'signed positive'),
        ('0009', 10, 'leading zeros'),
        ('0', 10, 'nothing but a zero'),
        ('', 10, 'nothing at all'),
        ('abc', 10, 'no digits in this base'),
        ('7f', 16, 'hexadecimal'),
        ('0X1F', 16, 'with the prefix'),
        ('0x1f', 0, 'base worked out, hexadecimal'),
        ('017', 0, 'base worked out, octal'),
        ('91', 0, 'base worked out, ten'),
        ('zz', 36, 'base thirty-six'),
        ('129', 8, 'a digit the base does not have'),
        ('9999999999', 10, 'too big'),
        ('99999999999999999999', 10, 'far too big'),
        ('4294967296', 10, 'exactly one past a longword'),
        ('12345678901234567890', 10, 'twenty digits'),
        ('FFFFFFFFFF', 16, 'too big in hexadecimal'),
        ('-9999999999', 10, 'too big the other way'),
        ('2147483647', 10, 'exactly the largest'),
        ('2147483648', 10, 'one past it'),
        ('123abc', 10, 'something after the number'),
        ('12', -1, 'a base below zero'),
        ('12', 1, 'a base of one'),
        ('12', 37, 'a base past thirty-six')):
    strtol_case(text, base, nm)

# H'22323A
def keypad_case(name, fresh, x=0x00, y=0x00, extra=None):
    e = collections.OrderedDict()
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    # The picker put away: the keypad does not use it, and the queue this
    # fill builds is not one the picker's own bookkeeping agrees with.
    e.update(w16(0x0011A1CC, 0x0000))
    e.update(w16(0x0011A1D0, 0x0000))
    e.update(w16(0x0011A1D2, 0x0000))
    if extra: e.update(extra)
    add('number_keypad_screen (%s)'%name, keypad_fill(e),
        {'addr':'22323A','regs':{'er6':'%02X'%fresh}},
        {'symbol':'_number_keypad_screen','regs':{'er0':'%02X'%fresh}})

def key(box, v):
    return w16(PLAIN_TABLE + 0x12*box + 0x08, v)

keypad_case('the first pass', 0x01)
keypad_case('the first pass, something already typed', 0x01, extra=field('12'))
keypad_case('nothing pressed', 0x00, extra=field('12'))
KX, KY = 0x0C, 0x24
for v, nm in ((0x001B, 'a zero'), (0x001C, 'a one'), (0x0024, 'a nine'),
              (0x000E, 'the rub-out'), (0x001A, 'the way out'),
              (0x0019, 'the answer'), (0x0055, 'a key nothing claims')):
    for text, tn in (('', 'nothing typed'), ('2', 'one digit'),
                     ('257', 'three digits'), ('2570', 'four digits')):
        keypad_case('%s, %s'%(nm, tn), 0x00, KX, KY,
                    extra=dict(list(key(1, v).items()) + list(field(text).items())))
# The answer, on each of the two screens and with the queue flags each way.
for sc, qn, qe, nm in ((0x47, 0x01, 0x00, 'the queue screen'),
                       (0x47, 0x00, 0x00, 'the queue screen, no queue'),
                       (0x46, 0x01, 0x01, 'a queue being edited'),
                       (0x46, 0x01, 0x00, 'a queue not being edited'),
                       (0x44, 0x01, 0x01, 'the screen that is left alone'),
                       (0x30, 0x01, 0x01, 'the other one left alone')):
    for text, tn in (('257', 'a number in the first list'),
                     ('259', 'one with a category of four'),
                     ('262', 'one in the second list'),
                     ('22', 'one of the odd two'),
                     ('265', 'the other one refused'),
                     ('999', 'a number in none of them')):
        keypad_case('the answer, %s, %s'%(nm, tn), 0x00, KX, KY,
                    extra=dict(list(key(1, 0x0019).items()) +
                               list(field(text).items()) +
                               list(b8(0x0011A169, sc).items()) +
                               list(b8(0x0011A174, qn).items()) +
                               list(b8(0x0011A178, qe).items())))
# The way out, both screens and both queue flags.
for sc, qe, at, nm in ((0x47, 0x00, 0x0000, 'the queue screen, nothing found'),
                       (0x47, 0x00, 0x0002, 'the queue screen, something found'),
                       (0x46, 0x01, 0x0000, 'a queue being edited'),
                       (0x46, 0x00, 0x0000, 'no queue being edited')):
    keypad_case('the way out, %s'%nm, 0x00, KX, KY,
                extra=dict(list(key(1, 0x001A).items()) +
                           list(field('257').items()) +
                           list(b8(0x0011A169, sc).items()) +
                           list(b8(0x0011A178, qe).items()) +
                           list(w16(0x0011A1BE, at).items()) +
                           list(w16(0x0011A1C0, 0x0101).items())))

ARROWS_UP = dict(list(b8(0x0011B3D6, 0x01).items()) +
                 list(b8(0x0011B3D7, 0x01).items()))
for nm, arrived, relayout, sc, ex in (
        ('just arrived', 1, 0, 0x46, None),
        ('just arrived, the queue screen', 1, 0, 0x47,
         dict(list(b8(0x0011A174, 0x01).items()))),
        ('just arrived, the arrows already lit', 1, 0, 0x47,
         dict(list(b8(0x0011A174, 0x01).items()) + list(ARROWS_UP.items()))),
        ('laid out again', 0, 1, 0x47, dict(list(b8(0x0011A174, 0x01).items()))),
        ('laid out again, the arrows already lit', 0, 1, 0x47,
         dict(list(b8(0x0011A174, 0x01).items()) + list(ARROWS_UP.items()))),
        ('a plain pass', 0, 0, 0x46, None),
        ('a plain pass, the arrows already lit', 0, 0, 0x47,
         dict(ARROWS_UP.items())),
        ('a plain pass, the queue screen', 0, 0, 0x47, None)):
    e = keypad_extra(ex)
    e.update(b8(0x0011A169, sc))
    dispatch_case(nm, sc, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_46 (%s, %02X)'%(nm, sc)

# ------------------------------------------------- screen H'43, the strip
PIC_AT      = 0x000E3000
PIC_STEP    = 0x0040
STRIP_ITEMS = 0x000E7000

def strip_pictures(widths):
    """A run-length picture per entry, each its own width and height."""
    d = collections.OrderedDict()
    d['0E3000:800']='00'
    for k, (w, h) in enumerate(widths):
        at = PIC_AT + PIC_STEP*k
        d.update(w16(at + 0, w))
        d.update(w16(at + 2, h))
        for j in range(0x30):
            d.update(b8(at + 4 + j, (0xFF, 0xFE, 0xFC, 0xFB)[(k + j) % 4]))
    return d

# Widths that do not divide the strip evenly, so a row wraps part-way and
# the entries of a row are told apart by how wide they are.
STRIP_WIDTHS = [(0x0E, 0x0C), (0x16, 0x10), (0x0A, 0x08), (0x1E, 0x14),
                (0x12, 0x0E), (0x0C, 0x0A), (0x1A, 0x12), (0x10, 0x0C),
                (0x14, 0x10), (0x0E, 0x08), (0x18, 0x14), (0x0A, 0x0C)]

def strip_extra(count=0x000A, pos=0x0004, extra=None):
    e = size_extra()
    e.pop('118DCE:10', None); e.pop('118E4A:10', None)
    e.update(block(0x001191EC))
    e.update(strip_pictures(STRIP_WIDTHS))
    e['11B3CE:20']='00'
    e['11BBAA:100']='00'
    # Both buffers given a pattern that changes from one line to the next:
    # a copy that moved the wrong number of rows would otherwise put the
    # same byte back where it came from and leave nothing to compare.
    for row in range(0x10, 0x70):
        e['%06X:50'%(0x00040000 + 0x50*row)] = '%02X'%((row*0x0B + 0x13) & 0xFF)
        e['%06X:50'%(0x00044B00 + 0x50*row)] = '%02X'%((row*0x1D + 0x47) & 0xFF)
    # The area the strip fills: three rows of H'14 with room for about
    # three of the wider pictures across.
    e.update(w16(0x0011A1D4, 0x0010))     # left
    e.update(w16(0x0011A1D6, 0x0050))     # right
    e.update(w16(0x0011A1D8, 0x0030))     # the first row's baseline
    e.update(w16(0x0011A1DA, 0x0058))     # the last row's
    e.update(w16(0x0011A1DC, 0x0014))     # and a row's height
    e.update(w16(0x0011A1CC, pos))        # where the cursor is
    e.update(w16(0x0011A1D0, 0x0000))     # the first entry, less one
    e.update(w16(0x0011A1D2, count))      # and the last
    e.update(b8(0x0011A1DE, 0x00))
    e.update(b8(0x0011A1DF, 0x00))
    e.update(b8(0x0011A1E0, 0x00))
    e.update(b8(0x0011A1E4, 0x00))
    e.update(b8(0x0011A1E8, 0x00))
    e.update(b8(0x0011A1E9, 0x00))
    e.update(b8(0x0011A1EA, 0x00))
    e.update(b8(0x0011B0A3, 0x30))
    e.update(b8(0x0011A169, 0x43))
    # The cache of pattern numbers, and a queue entry for each: the offset
    # byte moves the descriptor the picture comes from, and the two top bits
    # of the second byte say which way round it is drawn.
    e['11B3D8:40']='00'
    e.update(w16(0x0011B3D8, count))
    # The cache holds one less than the descriptor each entry wants and the
    # entry's own offset byte makes up the difference, so that dropping the
    # offset would pick a different picture.
    for k in range(1, 0x11):
        e.update(w16(0x0011B3D8 + 2*k, (k - 2) % len(STRIP_WIDTHS)))
        q = 0x11BBAA + 13*k
        e.update(b8(q + 0x0A, 0x01))
        e.update(b8(q + 0x01, (0x00, 0x40, 0x80, 0xC0)[k % 4]))
    # The catalogue moved out of the way. The shared fill puts it at
    # H'0E9000, which is inside the scratch buffer -- and this screen unpacks
    # a picture over the whole of that buffer before it draws the strip, so
    # by the time the strip is drawn every descriptor is zero and every
    # picture pointer with it. A null picture is read at address zero, where
    # the two images differ, and the case is then comparing the vector
    # tables.
    e.update(w32(0x00114DD2, STRIP_ITEMS))
    e['0E7000:500']='00'
    # Every descriptor points at the picture with its own index, and none is
    # category H'16, so the two facings that swap really do swap.
    for r in range(0x31):
        e.update(w32(STRIP_ITEMS + 0x18*r + 0x0C,
                     PIC_AT + PIC_STEP*(r % len(STRIP_WIDTHS))))
        e.update(w32(STRIP_ITEMS + 0x18*r + 0x04, 0x0034C6CD))
        e.update(w32(STRIP_ITEMS + 0x18*r + 0x08, 0x0034C6CD))
        e.update(b8(STRIP_ITEMS + 0x18*r + 0x17, 0x12))
    if extra: e.update(extra)
    return e

def strip_fill(count=0x000A, pos=0x0004, extra=None):
    c = {'fill': base_fill(strip_extra(count, pos, extra))}
    move_wipe(c)
    return c['fill']

def strip_state(at, x, y):
    d = collections.OrderedDict()
    d.update(w16(0x0011B3CE, x))
    d.update(w16(0x0011B3D0, y))
    d.update(w16(0x0011B3D2, at))
    return d

# H'22B4B4
for y, a, b, nm in ((0x0030, 0x0001, 0x000A, 'the whole queue'),
                    (0x0030, 0x0001, 0x0003, 'the first three'),
                    (0x0030, 0x0005, 0x0005, 'one on its own'),
                    (0x0058, 0x0001, 0x000A, 'starting on the last row'),
                    (0x0030, 0x0005, 0x0001, 'last before first'),
                    (0x0044, 0x0002, 0x0009, 'starting half way down')):
    add('queue_strip_run_draw (%s)'%nm, strip_fill(),
        {'addr':'22B4B4','regs':{'er6':'%04X'%y},
         'stack':{'4':'2:%04X'%a, '6':'2:%04X'%b}},
        {'symbol':'_queue_strip_run_draw','regs':{'er0':'%04X'%y,'er1':'%04X'%a,
                                                  'er2':'%04X'%b}})

# H'22B592
for at, back, on, nm in ((0x0005, 0x00, 0x00, 'in the middle, both out'),
                         (0x0005, 0x01, 0x01, 'in the middle, both already lit'),
                         (0x0000, 0x01, 0x01, 'at the first'),
                         (0x0000, 0x00, 0x00, 'at the first, already out'),
                         (0x000A, 0x01, 0x01, 'at the last'),
                         (0x000A, 0x00, 0x00, 'at the last, already out'),
                         (0x000B, 0x01, 0x01, 'past the last'),
                         (0xFFFF, 0x01, 0x01, 'before the first')):
    add('queue_strip_arrows (%s)'%nm,
        strip_fill(extra=dict(list(strip_state(at, 0x0010, 0x0030).items()) +
                              list(b8(0x0011A1DF, back).items()) +
                              list(b8(0x0011A1E0, on).items()))),
        {'addr':'22B592'}, {'symbol':'_queue_strip_arrows'})

# H'22B8AE and H'22B94A
add('queue_strip_scroll_up', strip_fill(),
    {'addr':'22B8AE'}, {'symbol':'_queue_strip_scroll_up'})
add('queue_strip_scroll_down', strip_fill(),
    {'addr':'22B94A'}, {'symbol':'_queue_strip_scroll_down'})

# H'22B9E8
for frm, nm in ((0x000A, 'from the last'), (0x0005, 'from the middle'),
                (0x0001, 'from the first'), (0x0002, 'from the second'),
                (0x0003, 'from the third')):
    add('queue_row_back (%s)'%nm, strip_fill(),
        {'addr':'22B9E8','result':'r6','regs':{'er6':'E6900'},
         'stack':{'4':'2:%04X'%frm}},
        {'symbol':'_queue_row_back','result':'r0',
         'regs':{'er0':'E6900','er1':'%04X'%frm}})

# A right edge a picture ends exactly on, which is where "one more would
# not fit" changes hands.
for frm, x1, nm in ((0x0005, 0x004C, 'the row ending on the edge'),
                    (0x0005, 0x004B, 'the row ending one short of it'),
                    (0x000A, 0x0040, 'a narrower strip')):
    add('queue_row_back (%s)'%nm,
        strip_fill(extra=w16(0x0011A1D6, x1)),
        {'addr':'22B9E8','result':'r6','regs':{'er6':'E6900'},
         'stack':{'4':'2:%04X'%frm}},
        {'symbol':'_queue_row_back','result':'r0',
         'regs':{'er0':'E6900','er1':'%04X'%frm}})

# H'22BA86
for upto, nm in ((0x0001, 'the first'), (0x0003, 'the third'),
                 (0x0005, 'the fifth'), (0x000A, 'the last'),
                 (0x0000, 'none of them')):
    add('queue_row_first (%s)'%nm, strip_fill(),
        {'addr':'22BA86','result':'r6','regs':{'er6':'%04X'%upto}},
        {'symbol':'_queue_row_first','result':'r0','regs':{'er0':'%04X'%upto}})

# H'22B698 and H'22B7B8
# Entry zero is not one of them: the cache word before the first is the
# strip's own x, so a walk that went below one would be reading the routine's
# own state and, through it, a picture pointer of zero.
for at, x, y, nm in ((0x0001, 0x001E, 0x0030, 'from the first'),
                     (0x0002, 0x0012, 0x0044, 'back to the first row'),
                     (0x0002, 0x0012, 0x0030, 'back past the first row'),
                     (0x0003, 0x001B, 0x0044, 'exactly on the left edge'),
                     (0x0003, 0x0048, 0x0044, 'exactly on the last row'),
                     (0x0003, 0x0031, 0x0030, 'exactly on the right edge'),
                     (0x0003, 0x004E, 0x0030, 'near the right edge'),
                     (0x0004, 0x0020, 0x0044, 'on the second row'),
                     (0x0008, 0x004C, 0x0058, 'on the last row'),
                     (0x000A, 0x0030, 0x0058, 'at the last'),
                     (0x0001, 0x000F, 0x0030, 'at the first')):
    for addr, sym, wnm in (('22B698','_queue_strip_forward','forward'),
                           ('22B7B8','_queue_strip_back','back')):
        add('queue_strip_%s (%s)'%(wnm, nm),
            strip_fill(extra=strip_state(at, x, y)),
            {'addr':addr}, {'symbol':sym})

# H'22B3A2
def strip_case(name, fresh, x=0x00, y=0x00, pos=0x0004, extra=None):
    e = collections.OrderedDict()
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    if extra: e.update(extra)
    add('queue_strip_screen (%s)'%name, strip_fill(0x000A, pos, e),
        {'addr':'22B3A2','regs':{'er6':'%02X'%fresh}},
        {'symbol':'_queue_strip_screen','regs':{'er0':'%02X'%fresh}})

for pos, nm in ((0x0001, 'the cursor on the first'),
                (0x0004, 'the cursor in the middle'),
                (0x0008, 'the cursor near the end'),
                (0x000A, 'the cursor on the last')):
    strip_case('the first pass, %s'%nm, 0x01, pos=pos)
strip_case('nothing pressed', 0x00, extra=strip_state(0x0004, 0x0030, 0x0044))
for box, v, nm in ((1, 0x0040, 'the back arrow'), (2, 0x0041, 'the on arrow'),
                   (3, 0x001A, 'the way out'), (1, 0x0055, 'a key nothing claims')):
    pass
for box, v, nm in ((1, 0x0040, 'the back arrow'), (2, 0x0041, 'the on arrow'),
                   (3, 0x001A, 'the way out'), (1, 0x0055, 'a key nothing claims')):
    bx = 0x0C + 0x10*((box - 1) % 12)
    by = 0x24 + 0x10*((box - 1) // 12)
    for back, on, sn in ((0x01, 0x01, 'both arrows lit'),
                         (0x00, 0x00, 'neither arrow lit')):
        strip_case('%s, %s'%(nm, sn), 0x00, bx, by,
                   extra=dict(list(w16(PLAIN_TABLE + 0x12*box + 0x08, v).items()) +
                              list(strip_state(0x0004, 0x0030, 0x0044).items()) +
                              list(b8(0x0011A1DF, back).items()) +
                              list(b8(0x0011A1E0, on).items())))
# The way out, with the slot already holding the screen it goes to and the
# arrival flag up: that is the one thing the slot argument decides.
strip_case('the way out, the slot already there', 0x00, 0x2C, 0x24,
           extra=dict(list(w16(PLAIN_TABLE + 0x12*3 + 0x08, 0x001A).items()) +
                      list(strip_state(0x0004, 0x0030, 0x0044).items()) +
                      list(b8(0x0011A16A, 0x30).items()) +
                      list(b8(0x0011B0A8, 0x01).items())))

for nm, arrived, relayout, pos in (('just arrived', 1, 0, 0x0004),
                                   ('just arrived, on the first', 1, 0, 0x0001),
                                   ('just arrived, on the last', 1, 0, 0x000A),
                                   ('laid out again', 0, 1, 0x0004),
                                   ('a plain pass', 0, 0, 0x0004)):
    e = strip_extra(0x000A, pos)
    e.update(strip_state(0x0004, 0x0030, 0x0044))
    dispatch_case(nm, 0x43, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_43 (%s)'%nm

# ------------------------------------------------- screen H'42, the queue panel
# A third copy of the test font, at the address the status bar's text_draw is
# given as a constant.
# H'37C of it, not H'400: the copy at H'119A66 begins that far along and
# the entries past there are for characters no case draws.
FONT4 = collections.OrderedDict()
FONT4['1196EA:37C']='00'
for k, v in _td.items():
    a, span = k.split(':')
    at, n = int(a, 16), int(span, 16)
    if 0x0E1000 <= at and at + n <= 0x0E137C:
        FONT4['%06X:%s' % (0x001196EA + (at - 0x0E1000), span)] = v

# The catalogue put where the machine really keeps it -- H'500000, in the
# flash window -- rather than in RAM. Two of this screen's keys add the
# markers H'3FE and H'3FF, and a table of H'400 entries is H'6000 bytes:
# there is nowhere in RAM it fits that the screen's own picture does not
# unpack over.
EDIT_ITEMS = 0x00500000
EDIT_PICS  = 0x000E3000

def edit_extra(pos=0x0004, extra=None):
    e = size_extra()
    e.pop('118DCE:10', None); e.pop('118E4A:10', None)
    e.update(block(0x00119194))
    e.update(FONT4)
    # A picture per catalogue entry, and the catalogue out of the scratch
    # buffer's way -- this screen unpacks over the whole of it.
    e['0E3000:400']='00'
    for k in range(8):
        at = EDIT_PICS + 0x40*k
        e.update(w16(at + 0, 0x0010 + 4*k))
        e.update(w16(at + 2, 0x000C + 2*k))
        for j in range(0x30):
            e.update(b8(at + 4 + j, (0xFF, 0xFE, 0xFC, 0xFB)[(k + j) % 4]))
    e.update(w32(0x00114DD2, EDIT_ITEMS))
    e['500000:6000']='00'
    for r in list(range(0x32)) + [0x03FE, 0x03FF]:
        e.update(w32(EDIT_ITEMS + 0x18*r + 0x04, 0x0034C6CD))
        e.update(w32(EDIT_ITEMS + 0x18*r + 0x08, 0x0034C6CD))
        e.update(w32(EDIT_ITEMS + 0x18*r + 0x0C, EDIT_PICS + 0x40*(r % 8)))
        e.update(w16(EDIT_ITEMS + 0x18*r + 0x14, 0x0100 + r))
        # Three kinds, so that the sixth control takes each of its three
        # shapes: H'12 to H'19 is one, H'16 is another, anything else the
        # third.
        e.update(b8(EDIT_ITEMS + 0x18*r + 0x17,
                    (0x12, 0x16, 0x05)[r % 3]))
    # The three tables of pictures the panel's controls are drawn from.
    e['11581E:100']='00'
    for k in range(0x40):
        e.update(w32(0x0011581E + 4*k, 0x0034C6CD))
    # The width strip: eleven boxes in a row, and the picture that marks one.
    e['11524E:60']='00'
    for i in range(11):
        at = 0x0011524E + 8*i
        e.update(w16(at + 0, 0x0060 + 6*i))
        e.update(w16(at + 2, 0x00A4))
        e.update(w16(at + 4, 0x0064 + 6*i))
        e.update(w16(at + 6, 0x00AA))
    e['11B2D0:04']='00'
    # The status bar along the top.
    e['11B2F0:10']='00'
    e['11541E:100']='00'
    e.update(b8(0x00FFFEEB, 0x03))      # a foot number, drawn as digits
    e.update(b8(0x00FFFEE2, 0x04))
    e.update(b8(0x00FFFEFA, 0x98))
    e.update(b8(0x00FFFEF7, 0x08))
    # What the status bar last drew, agreeing with the above: only the pass
    # that forces every word to H'FFFF has anything to do.
    e.update(w16(0x0011B2F2, 0x0003))
    e.update(w16(0x0011B2F0, 0x0004))
    e.update(w16(0x0011B2F8, 0x0010))
    e.update(w16(0x0011B2FA, 0x0008))
    e.update(w16(0x0011B2F4, 0x0080))
    e.update(w16(0x0011B2F6, 0x0008))
    # And the two arrows, already lit, so that only the pass with the flag
    # up draws them.
    e.update(b8(0x0011B3D6, 0x01))
    e.update(b8(0x0011B3D7, 0x01))
    # The panel's boxes left pressed, so that putting one back to plain is
    # something rather than nothing: hitbox_set_state returns at once when
    # the box is already in the state asked for.
    for bk in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
               0x10, 0x11, 0x12, 0x13, 0x14, 0x15):
        e.update(b8(PLAIN_TABLE + 0x12*bk + 0x10, 0x01))
    # The queue itself: sixteen entries, each with flags of its own.
    e['11BBAA:200']='00'
    # The first record is not a record: its two bytes are how many entries
    # the queue holds, and the shift the add and the delete do is measured
    # from it. Left as a record the length comes out negative and the move
    # never ends.
    e.update(b8(0x0011BBAA, 0x0A))
    e.update(b8(0x0011BBAB, 0x00))
    for k in range(1, 0x11):
        q = 0x11BBAA + 13*k
        e.update(b8(q + 0x00, (0x01 + k) & 0xFF))
        e.update(b8(q + 0x01, (0x00, 0x40, 0x80, 0xC0, 0x40)[k % 5]))
        e.update(b8(q + 0x02, 0x20 + k))
        e.update(b8(q + 0x03, 0x30 + k))
        e.update(b8(q + 0x04, (0x00, 0x80)[k % 2]))
        e.update(b8(q + 0x07, (0x00, 0x08, 0x21, 0x32)[k % 4]))
        e.update(b8(q + 0x0A, k % 3))
    e['11B3D8:40']='00'
    e.update(w16(0x0011B3D8, 0x000A))
    for k in range(1, 0x11):
        e.update(w16(0x0011B3D8 + 2*k, k % 8))
    e.update(w16(0x0011A1C8, 0x0010))    # where the picker's row starts
    e.update(w16(0x0011A1CA, 0x0060))
    e.update(w16(0x0011A1CC, pos))       # where the picker is
    e.update(w16(0x0011A1D0, 0x0001))    # its first
    e.update(w16(0x0011A1D2, 0x000A))    # and its last
    e['11F280:20']='00'
    # The three live settings, and what the panel last saw of them.
    e.update(b8(0x0011A69E, 0x20))
    e.update(b8(0x0011A6A0, 0x30))
    e.update(b8(0x00FFFEEA, 0x05))
    e.update(w16(0x0011F282, 0x0005))
    e.update(w16(0x0011F284, 0x0030))
    e.update(w16(0x0011F286, 0x0020))
    e.update(b8(0x0011A184, 0x00))
    e.update(b8(0x0011A1E1, 0x00))
    e.update(b8(0x0011A1E2, 0x00))
    e.update(b8(0x0011A175, 0x01))
    e.update(b8(0x0011A18B, 0x00))
    e.update(w16(0x00114DE0, 0x1234))
    if extra: e.update(extra)
    return e

def edit_fill(pos=0x0004, extra=None):
    c = {'fill': base_fill(edit_extra(pos, extra))}
    move_wipe(c)
    return c['fill']

# H'201506, H'20150E, H'201516
for addr, sym, nm in (('201506', '_sew_param_a_get', 'the first parameter'),
                      ('20150E', '_sew_param_b_get', 'the second'),
                      ('201516', '_stitch_width_get', 'the width')):
    add('%s'%sym[1:], edit_fill(),
        {'addr':addr,'result':'r6l'}, {'symbol':sym,'result':'r0l'})

# H'22A570
for nm, fresh, ex in (('the first pass', 0x01, None),
                      ('nothing moved', 0x00, None),
                      ('the width moved', 0x00, b8(0x00FFFEEA, 0x07)),
                      ('the first parameter moved', 0x00, b8(0x0011A69E, 0x44)),
                      ('the second moved', 0x00, b8(0x0011A6A0, 0x55)),
                      ('all three moved', 0x00,
                       dict(list(b8(0x00FFFEEA, 0x07).items()) +
                            list(b8(0x0011A69E, 0x44).items()) +
                            list(b8(0x0011A6A0, 0x55).items()))),
                      ('the picker at its first', 0x00,
                       dict(list(b8(0x00FFFEEA, 0x07).items()) +
                            list(w16(0x0011A1CC, 0x0001).items()))),
                      ('one position only', 0x00,
                       dict(list(b8(0x00FFFEEA, 0x07).items()) +
                            list(w16(0x0011A1D2, 0x0001).items())))):
    add('queue_settings_track (%s)'%nm, edit_fill(extra=ex),
        {'addr':'22A570','regs':{'er6':'%02X'%fresh}},
        {'symbol':'_queue_settings_track','regs':{'er0':'%02X'%fresh}})

# H'22AA8C
def edit_case(name, fresh, x=0x00, y=0x00, pos=0x0004, extra=None):
    e = collections.OrderedDict()
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    if extra: e.update(extra)
    add('queue_edit_screen (%s)'%name, edit_fill(pos, e),
        {'addr':'22AA8C','regs':{'er6':'%02X'%fresh}},
        {'symbol':'_queue_edit_screen','regs':{'er0':'%02X'%fresh}})

edit_case('the first pass', 0x01)
edit_case('the first pass, the other data set', 0x01, extra=b8(0x0057FF80, 0xAA))
edit_case('nothing pressed', 0x00)
edit_case('nothing pressed, the copy not taken yet', 0x00,
          extra=b8(0x0011F288, 0x01))
# The eleven boxes, pressed through box H'0B.
BOXK = 0x0B
def keyat(v):
    return w16(PLAIN_TABLE + 0x12*BOXK + 0x08, v)
BX, BY = 0x0C + 0x10*((BOXK - 1) % 12), 0x24 + 0x10*((BOXK - 1) // 12)
for v, nm in ((0x0040, 'a step back'), (0x0041, 'a step on'),
              (0x000E, 'the delete'), (0x0080, 'the first marker'),
              (0x0082, 'the second marker'), (0x0005, 'the first mirror'),
              (0x0006, 'the second mirror'), (0x0003, 'the tie-off'),
              (0x000C, 'the needle position'), (0x0007, 'the stitch length'),
              (0x0044, 'the flag box'), (0x0046, 'the counting box'),
              (0x0099, 'a key nothing claims')):
    for pos, pn in ((0x0004, 'in the middle'), (0x0001, 'at the first')):
        edit_case('%s, %s'%(nm, pn), 0x00, BX, BY, pos, extra=keyat(v))
# The three kinds of pattern the sixth control takes its shape from, and the
# two counters at the value that wraps.
for pos, nm in ((0x0002, 'a pattern of the second kind'),
                (0x0003, 'a pattern of the third kind')):
    for v, vn in ((0x0044, 'the flag box'), (0x0046, 'the counting box'),
                  (0x0007, 'the stitch length'), (0x000C, 'the needle position')):
        edit_case('%s, %s'%(vn, nm), 0x00, BX, BY, pos, extra=keyat(v))
edit_case('the stitch length at five', 0x00, BX, BY,
          extra=dict(list(keyat(0x0007).items()) +
                     list(b8(0x11BBAA + 13*4 + 0x07, 0x50).items())))
edit_case('the stitch length at nought', 0x00, BX, BY,
          extra=dict(list(keyat(0x0007).items()) +
                     list(b8(0x11BBAA + 13*4 + 0x07, 0x00).items())))
edit_case('the needle position at three', 0x00, BX, BY,
          extra=dict(list(keyat(0x000C).items()) +
                     list(b8(0x11BBAA + 13*4 + 0x07, 0x03).items())))
# The last box of the run the hit test covers, and one outside it.
for bk, bn in ((0x15, 'the last box'), (0x0F, 'a middle box')):
    bx = 0x0C + 0x10*((bk - 1) % 12)
    by = 0x24 + 0x10*((bk - 1) // 12)
    for v, vn in ((0x0003, 'the tie-off'), (0x0040, 'a step back')):
        edit_case('%s through %s'%(vn, bn), 0x00, bx, by,
                  extra=w16(PLAIN_TABLE + 0x12*bk + 0x08, v))
# A queue with no room left in it, which is the one thing that makes adding
# a marker answer no.
for v, vn in ((0x0080, 'the first marker'), (0x0082, 'the second marker')):
    edit_case('%s, the queue full'%vn, 0x00, BX, BY,
              extra=dict(list(keyat(v).items()) +
                         list(w16(0x0011B3D8, 0x03E8).items())))
edit_case('the counting box at two', 0x00, BX, BY,
          extra=dict(list(keyat(0x0046).items()) +
                     list(b8(0x11BBAA + 13*4 + 0x0A, 0x02).items())))

for nm, arrived, relayout, pos, ex in (
        ('just arrived', 1, 0, 0x0004, None),
        ('laid out again', 0, 1, 0x0004, None),
        ('a plain pass', 0, 0, 0x0004, None),
        ('a plain pass, at the first', 0, 0, 0x0001, None),
        ('a plain pass, the tie-off pressed', 0, 0, 0x0004,
         dict(list(keyat(0x0003).items()) +
              list(b8(0x00FFFED9, BX).items()) +
              list(b8(0x00FFFEDA, BY).items()))),
        ('a plain pass, a setting moved', 0, 0, 0x0004,
         b8(0x00FFFEEA, 0x07)),
        ('a plain pass, the strip pressed', 0, 0, 0x0004,
         dict(list(w16(PLAIN_TABLE + 0x12*2 + 0x08, 0x0017).items()) +
              list(b8(0x00FFFED9, 0x1C).items()) +
              list(b8(0x00FFFEDA, 0x24).items()))),
        ('a plain pass, a page back', 0, 0, 0x0004,
         dict(list(w16(PLAIN_TABLE + 0x12*2 + 0x08, 0x0018).items()) +
              list(b8(0x00FFFED9, 0x1C).items()) +
              list(b8(0x00FFFEDA, 0x24).items()))),
        ('a plain pass, the panel key', 0, 0, 0x0004,
         dict(list(w16(PLAIN_TABLE + 0x12*8 + 0x08, 0x0015).items()) +
              list(b8(0x00FFFED9, 0x7C).items()) +
              list(b8(0x00FFFEDA, 0x24).items()) +
              list(w32(0x0011A196, 0x000E6800).items()) +
              list(w16(0x000E6800, 0x0004).items()))),
        ('a plain pass, the way back', 0, 0, 0x0004,
         dict(list(w16(PLAIN_TABLE + 0x12*8 + 0x08, 0x0011).items()) +
              list(b8(0x00FFFED9, 0x7C).items()) +
              list(b8(0x00FFFEDA, 0x24).items()) +
              list(b8(0x0011B0A2, 0x07).items()))),
        ('a plain pass, the way out pressed', 0, 0, 0x0004,
         dict(list(w16(PLAIN_TABLE + 0x12*2 + 0x08, 0x001A).items()) +
              list(b8(0x00FFFED9, 0x1C).items()) +
              list(b8(0x00FFFEDA, 0x24).items())))):
    e = edit_extra(pos, ex)
    dispatch_case(nm, 0x42, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_42 (%s)'%nm

# ------------------------------------------------- screens H'17 and H'18
# A fourth copy of the test font, at the address the speed number's
# text_draw is given. H'3DA of it, not H'400: the picker's own block begins
# at H'11A1C0.
FONT5 = collections.OrderedDict()
# H'100 of it: the digits are at four times their own code, well inside
# that, and H'119DE6 plus H'3DA would reach the block at H'11A160 -- which
# is where the screen number itself lives.
FONT5['119DE6:100']='00'
for k, v in _td.items():
    a, span = k.split(':')
    at, n = int(a, 16), int(span, 16)
    if 0x0E1000 <= at and at + n <= 0x0E1100:
        FONT5['%06X:%s' % (0x00119DE6 + (at - 0x0E1000), span)] = v

SERVICE_ITEMS = 0x00500000

def service_extra(block_at=0x001181B0, extra=None):
    e = size_extra()
    e.pop('118DCE:10', None); e.pop('118E4A:10', None)
    e.update(block(block_at))
    e.update(FONT4)
    e.update(FONT5)
    # The catalogue out of the scratch buffer's way again.
    e.update(w32(0x00114DD2, SERVICE_ITEMS))
    e['500000:6000']='00'
    for r in list(range(0x32)) + [0x03FE, 0x03FF]:
        e.update(w32(SERVICE_ITEMS + 0x18*r + 0x04, 0x0034C6CD))
        e.update(w32(SERVICE_ITEMS + 0x18*r + 0x08, 0x0034C6CD))
        e.update(w32(SERVICE_ITEMS + 0x18*r + 0x0C, 0x0034C6CD))
        e.update(w16(SERVICE_ITEMS + 0x18*r + 0x14, 0x0100 + r))
        e.update(b8(SERVICE_ITEMS + 0x18*r + 0x17, (0x12, 0x10, 0x05)[r % 3]))
    e['1158CE:400']='00'
    for k in range(0x100):
        e.update(w32(0x001158CE + 4*k, 0x0034C6CD))
    # The two lists the service screens fill their boxes from.
    e['115A06:40']='00'
    e.update(w16(0x00115A06, 0x0004))
    for v in range(1, 5):
        e.update(w16(0x00115A06 + 2*v, v))
    e['115A20:40']='00'
    e.update(w16(0x00115A20, 0x0006))
    for v in range(1, 7):
        e.update(w16(0x00115A20 + 2*v, v))
    # The ten marks' inputs and what was last drawn for them.
    e['11B340:20']='00'
    e.update(b8(0x00FFFEC0, 0x05))
    e.update(b8(0x00FFFEC1, 0x0A))
    e.update(b8(0x00FFFEC4, 0x10))
    e.update(b8(0x00FFFEC5, 0x00))
    e.update(b8(0x00FFFEC7, 0x01))
    e.update(b8(0x00FFFEF8, 0x0C))
    e.update(w32(0x0057FF82, 0x00000E10))
    e.update(w32(0x0057FF86, 0x0000007B))
    e.update(b8(0x0011B0A3, 0x30))
    e.update(b8(0x0011A18B, 0x00))
    e.update(w16(0x00114DE0, 0x1234))
    # The width strip, as screen H'42's fill has it.
    e['11524E:60']='00'
    for i in range(11):
        at = 0x0011524E + 8*i
        e.update(w16(at + 0, 0x0060 + 6*i))
        e.update(w16(at + 2, 0x00A4))
        e.update(w16(at + 4, 0x0064 + 6*i))
        e.update(w16(at + 6, 0x00AA))
    e['11B2D0:04']='00'
    if extra: e.update(extra)
    return e

def service_fill(extra=None):
    c = {'fill': base_fill(service_extra(0x001181B0, extra))}
    move_wipe(c)
    return c['fill']

# H'21AC9E
for nm, fresh, ex in (
        ('the first pass', 0x01, None),
        ('nothing moved', 0x00, None),
        ('the marks already drawn', 0x00,
         dict(list(w16(0x0011B342, 0x0004).items()) +
              list(w16(0x0011B344, 0x0000).items()) +
              list(w16(0x0011B346, 0x0001).items()) +
              list(w16(0x0011B348, 0x0004).items()) +
              list(w16(0x0011B34A, 0x0008).items()) +
              list(w16(0x0011B34C, 0x0002).items()) +
              list(w16(0x0011B34E, 0x0001).items()) +
              list(w16(0x0011B350, 0x0008).items()) +
              list(w16(0x0011B352, 0x0000).items()) +
              list(w16(0x0011B354, 0x0010).items()) +
              list(w32(0x0011B356, 0x00000E10).items()) +
              list(w32(0x0011B35A, 0x0000007B).items()))),
        ('every input the other way', 0x00,
         dict(list(b8(0x00FFFEC0, 0x02).items()) +
              list(b8(0x00FFFEC1, 0x04).items()) +
              list(b8(0x00FFFEC4, 0x00).items()) +
              list(b8(0x00FFFEC7, 0x00).items()) +
              list(b8(0x00FFFEF8, 0x00).items()))),
        ('the tenth mark not looked at', 0x00, b8(0x00FFFEC5, 0x03)),
        ('the counters moved', 0x00,
         dict(list(w32(0x0057FF82, 0x00001C20).items()) +
              list(w32(0x0057FF86, 0x000004D2).items())))):
    add('service_marks_draw (%s)'%nm, service_fill(ex),
        {'addr':'21AC9E','regs':{'er6':'%02X'%fresh}},
        {'symbol':'_service_marks_draw','regs':{'er0':'%02X'%fresh}})

# H'216FA4
for v, nm in ((0, 'nought'), (7, 'one digit'), (0x0D05, 'four digits'),
              (0x0001E240, 'a hundred and twenty three thousand'),
              (0xFFFFFFFF, 'the biggest longword')):
    add('long_to_decimal (%s)'%nm, service_fill(),
        {'addr':'216FA4','regs':{'er6':'%X'%v},'stack':{'4':'4:000E6900'}},
        {'symbol':'_long_to_decimal','regs':{'er0':'%X'%v,'er1':'E6900'}})

# H'21148A
for b, nm in ((1, 'the first box'), (5, 'the fifth'), (0x30, 'the last')):
    add('hitbox_flag (%s)'%nm, service_fill(b8(PLAIN_TABLE + 0x12*b + 0x0A, 0x5A)),
        {'addr':'21148A','result':'r6l','regs':{'er6':'%04X'%b}},
        {'symbol':'_hitbox_flag','result':'r0l','regs':{'er0':'%04X'%b}})

# H'21CF9C
add('screen_stack_clear', service_fill(dict(list(b8(0x0011A18B, 0x03).items()) +
                                            list(b8(0x0011A17C, 0x01).items()))),
    {'addr':'21CF9C'}, {'symbol':'_screen_stack_clear'})

# The six little readers and writers, and the three that load from the row
# table at H'11A6E8.
ROW_AT = 0x000E6A00
def row_extra(extra=None):
    e = collections.OrderedDict()
    e['11A6E0:40']='00'
    e['0E6A00:40']='00'
    e.update(b8(0x0011A7A8, 0x02))
    for k in range(6):
        e.update(w32(0x0011A6E8 + 4*k, ROW_AT + 0x10*k))
    for k in range(6):
        for j in range(0x10):
            e.update(b8(ROW_AT + 0x10*k + j, (0x11 + 0x10*k + j) & 0xFF))
    e.update(b8(0x0011A69E, 0x20))
    e.update(b8(0x0011A6A0, 0x30))
    e.update(b8(0x00FFFEEA, 0x05))
    e['11A6D0:10']='00'
    if extra: e.update(extra)
    return e

for addr, sym, nm in (('2015EE', '_sew_param_a_load', 'the first parameter'),
                      ('201628', '_sew_param_b_load', 'the second'),
                      ('201662', '_stitch_width_load', 'the width')):
    for row, rn in ((0x02, 'row two'), (0x00, 'row nought'), (0x05, 'row five')):
        add('%s (%s)'%(sym[1:], rn),
            service_fill(row_extra(b8(0x0011A7A8, row))),
            {'addr':addr}, {'symbol':sym})

for addr, sym in (('20151E', '_stitch_width_get2'),
                  ('201526', '_sew_param_a_get2'),
                  ('20152E', '_sew_param_b_get2')):
    add(sym[1:], service_fill(row_extra()),
        {'addr':addr,'result':'r6l'}, {'symbol':sym,'result':'r0l'})

for addr, sym in (('201536', '_stitch_width_put'),
                  ('20153E', '_sew_param_a_put'),
                  ('20154A', '_sew_param_b_put')):
    for v in (0x07, 0x00, 0x64):
        add('%s (H\'%02X)'%(sym[1:], v), service_fill(row_extra()),
            {'addr':addr,'regs':{'er6':'%02X'%v}},
            {'symbol':sym,'regs':{'er0':'%02X'%v}})

for nm, arrived, sc in (('just arrived', 1, 0x17), ('a plain pass', 0, 0x17)):
    e = service_extra(0x001181B0)
    e.update(row_extra())
    dispatch_case(nm, 0x17, arrived, 0, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_17 (%s)'%nm

for nm, arrived, ex in (('just arrived', 1, None),
                        ('a plain pass', 0, None),
                        ('a plain pass, the way out', 0,
                         dict(list(w16(0x0011B10E, 0x0077).items()) +
                              list(w16(0x0011A17E, 0xFFFE).items())))):
    e = service_extra(0x001181C0, ex)
    e.update(row_extra())
    dispatch_case(nm, 0x18, arrived, 0, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['name'] = 'screen_body_18 (%s)'%nm

# ------------------------------------------------- the rest of the H'17 cluster
# H'228E80
for rec, nm in ((0x0004, 'the fourth'), (0x0001, 'the first'),
                (0x000A, 'the tenth')):
    add('queue_get_low6 (%s)'%nm, edit_fill(),
        {'addr':'228E80','result':'r6l','regs':{'er6':'%04X'%rec}},
        {'symbol':'_queue_get_low6','result':'r0l','regs':{'er0':'%04X'%rec}})

# H'20FCD6
def needle_extra(extra=None):
    e = service_extra(0x001181B0)
    e['11A860:20']='00'
    e.update(b8(0x00FFFEE5, 0x80))
    if extra: e.update(extra)
    return e

def needle_fill(extra=None):
    c = {'fill': base_fill(needle_extra(extra))}
    move_wipe(c)
    return c['fill']

for v, lim, fresh, nm in ((0x0014, 0x0028, 0x01, 'the first pass'),
                          (0x0000, 0x0028, 0x01, 'the first pass, at nought'),
                          (0x0028, 0x0028, 0x01, 'the first pass, at the top'),
                          (0x0014, 0x0028, 0x00, 'nothing moved'),
                          (0x0018, 0x0028, 0x00, 'moved up'),
                          (0x0010, 0x0028, 0x00, 'moved down'),
                          (0x0014, 0x0010, 0x00, 'the limit moved')):
    e = collections.OrderedDict()
    e.update(w16(0x0011A868, 0x0014))
    e.update(w16(0x0011A86A, 0x0059))
    e.update(w16(0x0011A86C, 0x0028))
    e.update(w16(0x0011A86E, 0x0080))
    e.update(w16(0x0011A870, 0x0019))
    add('bar_needle (%s)'%nm, needle_fill(e),
        {'addr':'20FCD6','regs':{'er6':'%04X'%v},
         'stack':{'4':'2:%04X'%lim, '6':'2:%04X'%fresh,
                  '8':'4:00040000', '0C':'2:0002'}},
        {'symbol':'_bar_needle','regs':{'er0':'%04X'%v,'er1':'%04X'%lim,
                                        'er2':'%04X'%fresh},
         'stack':{'4':'4:00040000','8':'4:00000002'}})
# The mark taken off and put back
for on, nm in ((0x00, 'the mark off'), (0x80, 'the mark on')):
    add('bar_needle (%s)'%nm,
        needle_fill(dict(list(b8(0x00FFFEE5, on).items()) +
                         list(w16(0x0011A86E, 0x0080 - on).items()) +
                         list(w16(0x0011A868, 0x0014).items()) +
                         list(w16(0x0011A86A, 0x0059).items()) +
                         list(w16(0x0011A86C, 0x0028).items()) +
                         list(w16(0x0011A870, 0x0019).items()))),
        {'addr':'20FCD6','regs':{'er6':'0014'},
         'stack':{'4':'2:0028', '6':'2:0000', '8':'4:00040000', '0C':'2:0002'}},
        {'symbol':'_bar_needle','regs':{'er0':'0014','er1':'0028','er2':'0000'},
         'stack':{'4':'4:00040000','8':'4:00000002'}})

# H'2012EC and H'20145E
def store_extra(extra=None):
    e = service_extra(0x001181B0)
    e['0E4010:800']='00'
    for r in range(0x20):
        for j in range(0x10):
            e.update(b8(0x000E4010 + 0x10*r + j, (0x21 + r + j) & 0x7F))
    e['57B6D0:80']='00'
    e.update(w16(0x00FFFEE0, 0x0004))
    e.update(b8(0x00FFFEFD, 0x00))
    e.update(b8(0x0011A7BD, 0x00))
    if extra: e.update(extra)
    return e

def store_fill(extra=None):
    c = {'fill': base_fill(store_extra(extra))}
    move_wipe(c)
    return c['fill']

for n, nm in ((0x0004, 'the fourth'), (0x0000, 'the first'),
              (0x0010, 'the sixteenth')):
    add('pattern_settings_write (%s)'%nm, store_fill(),
        {'addr':'2012EC','regs':{'er6':'%04X'%n}},
        {'symbol':'_pattern_settings_write','regs':{'er0':'%04X'%n}})

# The run the store walks: a category of H'10 with H'01 entries after it.
RUN = collections.OrderedDict()
for k, cat in ((4, 0x10), (5, 0x01), (6, 0x01), (7, 0x12)):
    RUN.update(b8(SERVICE_ITEMS + 0x18*k + 0x17, cat))
for nm, ex in (('a run of three', RUN),
               ('one on its own', None),
               ('the run one along',
                dict(list(RUN.items()) + list(b8(0x00FFFEFD, 0x01).items())))):
    add('pattern_settings_store (%s)'%nm, store_fill(ex),
        {'addr':'20145E'}, {'symbol':'_pattern_settings_store'})

# H'21B682
for nm, ex in (('nothing asked for', w16(0x0011B10E, 0xFFFF)),
               ('the way out', dict(list(w16(0x0011B10E, 0x0077).items()) +
                                    list(w16(0x0011A17E, 0xFFFE).items()))),
               ('somewhere else', dict(list(w16(0x0011B10E, 0x0031).items()) +
                                       list(w16(0x0011A17E, 0xFFFE).items())))):
    add('module_busy_screen (%s)'%nm, service_fill(ex),
        {'addr':'21B682','result':'r6l'},
        {'symbol':'_module_busy_screen','result':'r0l'})

# H'21CDA4
def settings_case(name, fresh, x=0x00, y=0x00, extra=None):
    e = collections.OrderedDict()
    e.update(row_extra())
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    e['11B37C:08']='00'
    e.update(b8(0x0011B37C, 0x41))
    e.update(b8(0x0011B37D, 0x42))
    e.update(b8(0x0011B37E, 0x43))
    if extra: e.update(extra)
    add('sew_settings_screen (%s)'%name, service_fill(e),
        {'addr':'21CDA4','regs':{'er6':'%02X'%fresh},'result':'r6l'},
        {'symbol':'_sew_settings_screen','regs':{'er0':'%02X'%fresh},
         'result':'r0l'})

settings_case('the first pass', 0x01)
settings_case('nothing pressed', 0x00)
BX2, BY2 = 0x0C, 0x24
for v, nm in ((0x007F, 'the reset'), (0x0019, 'the accept'),
              (0x001A, 'the cancel'), (0x006E, 'the width down'),
              (0x006F, 'the width up'), (0x0055, 'a key nothing claims')):
    settings_case(nm, 0x00, BX2, BY2,
                  extra=w16(PLAIN_TABLE + 0x12 + 0x08, v))
for v, nm in ((0x0077, 'the panel asking to leave'), (0x0019, 'the panel accept')):
    settings_case('%s'%nm, 0x00,
                  extra=dict(list(w16(0x0011B10E, v).items()) +
                             list(w16(0x0011A17E, 0xFFFE).items())))

# H'21D264
def needle_pos_case(name, fresh, x=0x00, y=0x00, extra=None):
    e = collections.OrderedDict()
    e.update(row_extra())
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    e['11B384:04']='00'
    e.update(b8(0x0011B384, 0x14))
    e.update(b8(0x0011B385, 0x0A))
    e.update(b8(0x00FFFEEC, 0x12))
    e.update(b8(0x00FFFEED, 0x28))
    e.update(w16(0x00FFFEFE, 0x0004))
    e.update(b8(0x0011A175, 0x00))
    e.update(b8(0x0011A168, 0x02))
    # What the bar last drew, agreeing with the value: left at zero the
    # difference is drawn from a remembered pixel of nought, and a rectangle
    # to y = H'FFFF lands outside the buffer.
    e['11A860:20']='00'
    e.update(w16(0x0011A868, 0x0014))
    e.update(w16(0x0011A86A, 0x0061))
    e.update(w16(0x0011A86C, 0x0028))
    e.update(w16(0x0011A86E, 0x0080))
    e.update(w16(0x0011A870, 0x0021))
    e.update(b8(0x00FFFEE5, 0x80))
    if extra: e.update(extra)
    add('needle_position_screen (%s)'%name, edit_fill(0x0004, e),
        {'addr':'21D264','regs':{'er6':'%02X'%fresh},'result':'r6l'},
        {'symbol':'_needle_position_screen','regs':{'er0':'%02X'%fresh},
         'result':'r0l'})

for q, qn in ((0x00, 'no queue'), (0x01, 'the queue showing')):
    needle_pos_case('the first pass, %s'%qn, 0x01, extra=b8(0x0011A175, q))
    needle_pos_case('nothing pressed, %s'%qn, 0x00, extra=b8(0x0011A175, q))
    for v, nm in ((0x0017, 'up'), (0x0018, 'down'), (0x007F, 'the reset'),
                  (0x0019, 'the accept'), (0x001A, 'the cancel'),
                  (0x0055, 'a key nothing claims')):
        needle_pos_case('%s, %s'%(nm, qn), 0x00, BX2, BY2,
                        extra=dict(list(w16(PLAIN_TABLE + 0x12 + 0x08, v).items()) +
                                   list(b8(0x0011A175, q).items())))
needle_pos_case('up at the top', 0x00, BX2, BY2,
                extra=dict(list(w16(PLAIN_TABLE + 0x12 + 0x08, 0x0017).items()) +
                           list(b8(0x0011B384, 0x28).items())))
needle_pos_case('down at nought', 0x00, BX2, BY2,
                extra=dict(list(w16(PLAIN_TABLE + 0x12 + 0x08, 0x0018).items()) +
                           list(b8(0x0011B384, 0x00).items())))
needle_pos_case('the accept while sewing', 0x00, BX2, BY2,
                extra=dict(list(w16(PLAIN_TABLE + 0x12 + 0x08, 0x0019).items()) +
                           list(b8(0x00114DC6, 0x80).items())))
needle_pos_case('the cancel with a stack', 0x00, BX2, BY2,
                extra=dict(list(w16(PLAIN_TABLE + 0x12 + 0x08, 0x001A).items()) +
                           list(b8(0x0011A18B, 0x02).items()) +
                           list(b8(0x0011A18D, 0x27).items())))

# ------------------------------------------------- the two module screens
# The font the hoop labels are drawn with, taken from the case that already
# covers them: it has glyphs for "mm" as well as the digits, and text_draw
# with a glyph pointer of nought writes a thousand rows outside the buffer.
_hd = [c for c in cases if c['name'] == 'hoop_offsets_draw (both zero)'][0]['fill']
HOOPFONT = collections.OrderedDict()
for k, v in _hd.items():
    a = int(k.split(':')[0], 16)
    if 0x0E1500 <= a < 0x0E1700 or 0x00119A66 <= a < 0x00119C66:
        HOOPFONT[k] = v

def modscr_extra(extra=None):
    e = service_extra(0x001181B0)
    # The digits-only copy taken out first: its keys are already in the fill,
    # so an update would leave them where they are and the wipe that comes
    # with the hoop font would then cover them.
    for k in [k for k in e if 0x00119A66 <= int(k.split(':')[0], 16) < 0x00119C66]:
        del e[k]
    e.update(HOOPFONT)
    e['104C70:20']='00'
    e['11A610:40']='00'
    e['11A260:80']='00'
    e['11F290:40']='00'
    e['114D40:80']='00'
    e['114D90:40']='00'
    e['114DB0:20']='00'
    e['1040B0:10']='00'
    e.update(b8(0x00114DA2, 0x01))
    e.update(b8(0x00104C7A, 0x08))
    e.update(b8(0x00104C7B, 0xF8))
    e.update(b8(0x0011A660, 0x02))
    e.update(b8(0x00114D51, 0x1D))
    e.update(b8(0x0011F29C, 0x05))
    e.update(b8(0x00FFFEC5, 0x00))
    e.update(b8(0x0011A18B, 0x00))
    e.update(w16(0x0011B10E, 0xFFFF))
    e.update(w16(0x0011A17E, 0xFFFE))
    if extra: e.update(extra)
    return e

def modscr_fill(extra=None):
    c = {'fill': base_fill(modscr_extra(extra))}
    move_wipe(c)
    return c['fill']

BX3, BY3 = 0x0C, 0x24
def boxval(b, v):
    return w16(PLAIN_TABLE + 0x12*b + 0x08, v)

# H'230AF4
for b, nm in ((1, 'the reset'), (2, 'up and left'), (3, 'up'), (4, 'up and right'),
              (5, 'left'), (6, 'right'), (7, 'down and left'), (8, 'down'),
              (9, 'down and right'), (10, 'the way out'), (11, 'the other way out')):
    bx = 0x0C + 0x10*((b - 1) % 12)
    by = 0x24 + 0x10*((b - 1) // 12)
    add('hoop_nudge_screen (%s)'%nm,
        modscr_fill(dict(list(b8(0x00FFFED9, bx).items()) +
                         list(b8(0x00FFFEDA, by).items()) +
                         list(boxval(b, b).items()))),
        {'addr':'230AF4','result':'r6l'},
        {'symbol':'_hoop_nudge_screen','result':'r0l'}, steps=40000000)
add('hoop_nudge_screen (nothing pressed)', modscr_fill(),
    {'addr':'230AF4','result':'r6l'},
    {'symbol':'_hoop_nudge_screen','result':'r0l'})
add('hoop_nudge_screen (the module busy)',
    modscr_fill(dict(list(b8(0x00FFFED9, BX3).items()) +
                     list(b8(0x00FFFEDA, BY3).items()) +
                     list(boxval(1, 1).items()) +
                     list(b8(0x00114D50, 0x20).items()))),
    {'addr':'230AF4','result':'r6l'},
    {'symbol':'_hoop_nudge_screen','result':'r0l'})
add('hoop_nudge_screen (already in this state)',
    modscr_fill(dict(list(b8(0x00FFFED9, BX3).items()) +
                     list(b8(0x00FFFEDA, BY3).items()) +
                     list(boxval(1, 1).items()) +
                     list(b8(0x00114D8E, 0x0B).items()))),
    {'addr':'230AF4','result':'r6l'},
    {'symbol':'_hoop_nudge_screen','result':'r0l'}, steps=40000000)

# H'230C42. Boxes three, four and five end in a wait the harness cannot
# finish, so only the two that do not are pressed.
for b, v, nm in ((1, 1, 'the switches shown'), (2, 2, 'the hoop sent home'),
                 (1, 9, 'a key nothing claims')):
    add('module_settings_screen (%s)'%nm,
        modscr_fill(dict(list(b8(0x00FFFED9, BX3).items()) +
                         list(b8(0x00FFFEDA, BY3).items()) +
                         list(boxval(b, v).items()))),
        {'addr':'230C42','result':'r6l'},
        {'symbol':'_module_settings_screen','result':'r0l'}, steps=40000000)
add('module_settings_screen (nothing pressed)', modscr_fill(),
    {'addr':'230C42','result':'r6l'},
    {'symbol':'_module_settings_screen','result':'r0l'})
add('module_settings_screen (the module busy)',
    modscr_fill(dict(list(b8(0x00FFFED9, BX3).items()) +
                     list(b8(0x00FFFEDA, BY3).items()) +
                     list(boxval(1, 1).items()) +
                     list(b8(0x00114D50, 0x20).items()))),
    {'addr':'230C42','result':'r6l'},
    {'symbol':'_module_settings_screen','result':'r0l'})
for nm, ex in (('the way out, free to go',
                dict(list(w16(0x0011B10E, 0x0077).items()) +
                     list(b8(0x00FFFEC5, 0x03).items()))),
               ('the way out, still reporting',
                dict(list(w16(0x0011B10E, 0x0077).items()) +
                     list(b8(0x00FFFEC5, 0x0D).items()) +
                     list(b8(0x00114DA3, 0x01).items()))),
               ('somewhere else asked for', w16(0x0011B10E, 0x0031))):
    add('module_settings_screen (%s)'%nm, modscr_fill(ex),
        {'addr':'230C42','result':'r6l'},
        {'symbol':'_module_settings_screen','result':'r0l'}, steps=40000000)


# ------------------------------------------- the picker strip five screens share
# H'2299A6 and the four helpers under it. The queue, the picker and the
# panel are all as screen H'42's fill has them; what is added is the table of
# ranges at H'11EE80, the digits font the box numbers are drawn with, and the
# two bytes of the store's own size the bar across the bottom is a percentage
# of.
def pick_extra(pos=0x0004, extra=None):
    e = edit_extra(pos)
    e.update(FONT5)
    # The ranges: eighteen pairs, every third one a single entry so that the
    # mark is drawn both ways.
    e['11EE80:60']='00'
    for w in range(0x18):
        lo = 1 + (w % 6)
        hi = lo + (0, 1, 3)[w % 3]
        e.update(w16(0x0011EE80 + 4*w + 0, lo))
        e.update(w16(0x0011EE80 + 4*w + 2, hi))
    e.update(w16(0x0011A1EC, 0x0001))    # the range the first box stands for
    e.update(w16(0x0011A1EE, 0x0001))    # the box the cursor is on
    e.update(w16(0x0011A1CE, 0x0001))    # and the range it is
    e.update(b8(0x0011A1E3, 0x00))
    e.update(b8(0x0011A1E4, 0x00))
    e.update(b8(0x0011F280, 0x01))
    e.update(b8(0x0011F281, 0x01))
    e.update(w16(0x0011B3D4, 0x0001))
    # Five hundred out of a thousand: the bar half drawn, so that both
    # rectangles are something.
    e.update(b8(0x00578000, 0xF4))
    e.update(b8(0x00578001, 0x01))
    # The four things that hold the "are you sure" screen off, all clear, so
    # that the key which asks for one really puts it up.
    e.update(b8(0x0011A179, 0x01))
    e.update(b8(0x0011A173, 0x00))
    e.update(b8(0x00114DC6, 0x00))
    # Both buffers given a pattern that changes from one line to the next:
    # the paging copies rows about, and with every row the same a copy that
    # took the wrong one would put the same byte back.
    for row in range(0x08, 0x9C):
        e['%06X:50'%(0x00040000 + 0x50*row)] = '%02X'%((row*0x0B + 0x13) & 0xFF)
        e['%06X:50'%(0x00044B00 + 0x50*row)] = '%02X'%((row*0x1D + 0x47) & 0xFF)
    if extra: e.update(extra)
    return e

def pick_fill(pos=0x0004, extra=None):
    c = {'fill': base_fill(pick_extra(pos, extra))}
    move_wipe(c)
    return c['fill']

def percent(n):
    raw = n * 10
    return dict(list(b8(0x00578000, raw & 0xFF).items()) +
                list(b8(0x00578001, (raw >> 8) & 0xFF).items()))

# H'22A400
for n, nm in ((0x32, 'half full'), (0x00, 'empty'), (0x64, 'full'),
              (0x01, 'one'), (0x63, 'ninety nine'),
              (0x65, 'past the end'), (0x1388, 'far past the end')):
    add('percent_bar_draw (%s)'%nm, pick_fill(extra=percent(n)),
        {'addr':'22A400'}, {'symbol':'_percent_bar_draw'})
add('percent_bar_draw (below nought)',
    pick_fill(extra=dict(list(b8(0x00578000, 0x00).items()) +
                         list(b8(0x00578001, 0x90).items()))),
    {'addr':'22A400'}, {'symbol':'_percent_bar_draw'})

# H'2298E4
for a, b, st, nm in ((0x0001, 0x000F, 0x0001, 'the whole strip'),
                     (0x0001, 0x0005, 0x0003, 'the first five'),
                     (0x000B, 0x000F, 0x000B, 'the last five'),
                     (0x0005, 0x0005, 0x0063, 'one on its own, three digits'),
                     (0x0005, 0x0001, 0x0001, 'last before first')):
    add('hitbox_numbers_draw (%s)'%nm, pick_fill(),
        {'addr':'2298E4','regs':{'er6':'%04X'%a},
         'stack':{'4':'2:%04X'%b, '6':'2:%04X'%st}},
        {'symbol':'_hitbox_numbers_draw',
         'regs':{'er0':'%04X'%a,'er1':'%04X'%b,'er2':'%04X'%st}})

# H'22A2BE
for a, b, w, nm in ((0x0001, 0x000F, 0x0001, 'the whole strip'),
                    (0x0001, 0x0005, 0x0003, 'the first five'),
                    (0x000B, 0x000F, 0x000B, 'the last five'),
                    (0x0000, 0x000F, 0x0001, 'from nought'),
                    (0x0008, 0x0008, 0x0002, 'one on its own')):
    add('picker_range_mark (%s)'%nm, pick_fill(),
        {'addr':'22A2BE','regs':{'er6':'%04X'%a},
         'stack':{'4':'2:%04X'%b, '6':'2:%04X'%w}},
        {'symbol':'_picker_range_mark',
         'regs':{'er0':'%04X'%a,'er1':'%04X'%b,'er2':'%04X'%w}})

# H'22A33C. Range nought is a single entry and range one spans two.
for box, w, nm in ((0x0008, 0x0000, 'a range of one'),
                   (0x0008, 0x0001, 'a range of two'),
                   (0x0001, 0x0002, 'a range of two, a box with a kind'),
                   (0x0009, 0x0004, 'a range further along')):
    add('picker_range_close (%s)'%nm, pick_fill(),
        {'addr':'22A33C','regs':{'er6':'%04X'%box},
         'stack':{'4':'2:%04X'%w}},
        {'symbol':'_picker_range_close',
         'regs':{'er0':'%04X'%box,'er1':'%04X'%w}})

# H'2299A6
def pbox(b):
    return (0x0C + 0x10*((b - 1) % 12), 0x24 + 0x10*((b - 1) // 12))

def pick_case(name, mode, x=0x00, y=0x00, extra=None):
    e = collections.OrderedDict()
    e.update(b8(0x00FFFED9, x)); e.update(b8(0x00FFFEDA, y))
    e.update(b8(0x0011B0A8, 0x00))
    if extra: e.update(extra)
    add('picker_strip_screen (%s)'%name, pick_fill(0x0004, e),
        {'addr':'2299A6','regs':{'er6':'%04X'%mode}},
        {'symbol':'_picker_strip_screen','regs':{'er0':'%04X'%mode}},
        steps=40000000)

pick_case('the first pass', 0x0001)
pick_case('the marks put back', 0x0002)
pick_case('a plain pass', 0x0003)
pick_case('a range to close', 0x0003, extra=b8(0x0011A1E3, 0x01))
# One of the fifteen: the flag byte is what tells a strip box from a key.
BXP, BYP = pbox(8)
pick_case('a box on the strip', 0x0003, BXP, BYP,
          extra=b8(PLAIN_TABLE + 0x12*8 + 0x0A, 0x5A))
pick_case('a box on the strip with a kind', 0x0003, *pbox(1),
          extra=b8(PLAIN_TABLE + 0x12*1 + 0x0A, 0x5A))
BXK, BYK = pbox(9)
def keyv(v, b=9):
    return w16(PLAIN_TABLE + 0x12*b + 0x08, v)
for v, nm, ex in ((0x0017, 'a page back', w16(0x0011A1EC, 0x000B)),
                  (0x0017, 'a page back at the start', None),
                  (0x0018, 'a page on', None),
                  (0x0018, 'a page on at the end', w16(0x0011A1EC, 0x00F1)),
                  (0x0018, 'a page on to the end',
                   w16(0x0011A1EC, 0x00EC)),
                  (0x0040, 'the arrow back', None),
                  (0x0040, 'the arrow back, not lit', b8(0x0011B3D6, 0x00)),
                  (0x0041, 'the arrow on', None),
                  (0x0041, 'the arrow on, not lit', b8(0x0011B3D7, 0x00)),
                  (0x000E, 'the question', None),
                  (0x0019, 'the accept', None),
                  (0x001A, 'the cancel', None),
                  (0x0055, 'a key nothing claims', None)):
    d = collections.OrderedDict(keyv(v))
    if ex: d.update(ex)
    pick_case(nm, 0x0003, BXK, BYK, extra=d)
# The two arrows blanked, so that a page that reaches an end puts one back.
# The cursor's box and the range it stands for told apart, so that a close
# taking one for the other is something.
pick_case('a range to close, the cursor along the strip', 0x0003,
          extra=dict(list(b8(0x0011A1E3, 0x01).items()) +
                     list(w16(0x0011A1EE, 0x0003).items()) +
                     list(w16(0x0011A1CE, 0x0005).items())))
# The last box the strip's own touch reaches.
pick_case('the last key of all', 0x0003, *pbox(0x16),
          extra=keyv(0x0019, 0x16))
pick_case('a page back one before the first', 0x0003, BXK, BYK,
          extra=dict(list(keyv(0x0017).items()) +
                     list(w16(0x0011A1EC, 0x0005).items())))
pick_case('a page back with the arrows down', 0x0003, BXK, BYK,
          extra=dict(list(keyv(0x0017).items()) +
                     list(w16(0x0011A1EC, 0x0006).items()) +
                     list(b8(0x0011F281, 0x00).items())))
pick_case('a page on with the arrows down', 0x0003, BXK, BYK,
          extra=dict(list(keyv(0x0018).items()) +
                     list(b8(0x0011F280, 0x00).items())))

# ------------------------------------------- the five bodies the strip serves
# H'227CC6, screen H'41
for nm, arrived, relayout, ex in (('just arrived', 1, 0, None),
                                  ('laid out again', 0, 1, None),
                                  ('a plain pass', 0, 0, None),
                                  ('a plain pass, the strip to lay out again',
                                   0, 0, b8(0x0011B0A9, 0x01)),
                                  # The fourth longword the arrival copies is
                                  # the box table pointer: with the table
                                  # already pinned to the same value, copying
                                  # it changes nothing. Pointed somewhere
                                  # blank to begin with, it does.
                                  ('just arrived, the table pointer moved',
                                   1, 0, w32(0x0011B0BA, 0x000E7800)),
                                  # The cursor down, so that the mode the
                                  # relayout asks for is one that does
                                  # something rather than one that is already
                                  # in that state.
                                  ('the strip to lay out again, the cursor down',
                                   0, 0, dict(list(b8(0x0011B0A9, 0x01).items()) +
                                              list(b8(0x0011A1E9, 0x00).items()) +
                                              list(b8(0x0011A1EA, 0x00).items()))),
                                  ('a press on the first key', 0, 0,
                                   dict(list(keyv(0x0019).items()) +
                                        list(b8(0x00FFFED9, BXK).items()) +
                                        list(b8(0x00FFFEDA, BYK).items())))):
    e = pick_extra(0x0004, ex)
    e.update(block(0x00118FF8))
    dispatch_case(nm, 0x41, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['steps'] = 40000000
    new[-1]['name'] = 'screen_body_41 (%s)'%nm

# H'227898, screen H'31, and H'2262EE, screen H'1F: the two module screens.
for sc, at, nm2 in ((0x31, 0x00118C84, 'the hoop moved by hand'),
                    (0x1F, 0x001182FC, "the module's own settings")):
    for nm, arrived, relayout, ex in (('just arrived', 1, 0, None),
                                      ('laid out again', 0, 1, None),
                                      ('a plain pass', 0, 0, None),
                                      ('just arrived, the table pointer moved',
                                       1, 0, w32(0x0011B0BA, 0x000E7800)),
                                      ('a press on the first box', 0, 0,
                                       dict(list(b8(0x00FFFED9, BX3).items()) +
                                            list(b8(0x00FFFEDA, BY3).items()) +
                                            list(boxval(1, 1).items())))):
        e = modscr_extra(ex)
        e.update(block(at))
        dispatch_case(nm, sc, arrived, relayout, 0, extra=e)
        move_wipe(new[-1])
        new[-1]['steps'] = 40000000
        new[-1]['name'] = 'screen_body_%02X (%s)'%(sc, nm)

# H'226D6E, screen H'2B: the three sewing settings.
for nm, arrived, relayout, press, ex in (
        ('just arrived', 1, 0, 0, None),
        ('laid out again', 0, 1, 0, None),
        ('a plain pass', 0, 0, 0, None),
        ('just arrived, the table pointer moved', 1, 0, 0,
         w32(0x0011B0BA, 0x000E7800)),
        # The width already at its top: the first pass then leaves the black
        # above the bar undrawn, and the pass that follows it has nothing to
        # put back, so a bar drawn from the wrong byte stays wrong.
        ('just arrived, the width at its top', 1, 0, 0,
         b8(0x00FFFEE7, 0x64)),
        ('a press on the first box', 0, 0, 1,
         w16(PLAIN_TABLE + 0x12 + 0x08, 0x007F))):
    e = collections.OrderedDict()
    e.update(row_extra())
    e['11B37C:08']='00'
    e.update(b8(0x0011B37C, 0x41))
    e.update(b8(0x0011B37D, 0x42))
    e.update(b8(0x0011B37E, 0x43))
    e.update(b8(0x00FFFEE7, 0x28))
    e.update(b8(0x00FFFEE4, 0x14))
    if ex: e.update(ex)
    if press:
        e.update(b8(0x00FFFED9, BX2)); e.update(b8(0x00FFFEDA, BY2))
    f = service_extra(0x001181B0, e)
    f.update(block(0x00118780))
    dispatch_case(nm, 0x2B, arrived, relayout, 0, extra=f)
    move_wipe(new[-1])
    new[-1]['steps'] = 40000000
    new[-1]['name'] = 'screen_body_2B (%s)'%nm

# H'227020, screen H'2D: the needle stop position.
for nm, arrived, relayout, press, ex in (
        ('just arrived', 1, 0, 0, None),
        ('laid out again', 0, 1, 0, None),
        ('a plain pass', 0, 0, 0, None),
        ('just arrived, the table pointer moved', 1, 0, 0,
         w32(0x0011B0BA, 0x000E7800)),
        ('a press on the first box', 0, 0, 1,
         w16(PLAIN_TABLE + 0x12 + 0x08, 0x0017))):
    e = collections.OrderedDict()
    e.update(row_extra())
    e['11B384:04']='00'
    e.update(b8(0x0011B384, 0x14))
    e.update(b8(0x0011B385, 0x0A))
    e.update(b8(0x00FFFEEC, 0x12))
    e.update(b8(0x00FFFEED, 0x28))
    e.update(w16(0x00FFFEFE, 0x0004))
    e.update(b8(0x0011A175, 0x00))
    e.update(b8(0x0011A168, 0x02))
    # No wide zero over H'11A860 here: the dispatch fill sets bytes of its
    # own in that range and a wipe added afterwards would cover them.
    e.update(w16(0x0011A868, 0x0014))
    e.update(w16(0x0011A86A, 0x0061))
    e.update(w16(0x0011A86C, 0x0028))
    e.update(w16(0x0011A86E, 0x0080))
    e.update(w16(0x0011A870, 0x0021))
    e.update(b8(0x00FFFEE5, 0x80))
    if ex: e.update(ex)
    if press:
        e.update(b8(0x00FFFED9, BX2)); e.update(b8(0x00FFFEDA, BY2))
    f = edit_extra(0x0004, e)
    f.update(block(0x001187FC))
    dispatch_case(nm, 0x2D, arrived, relayout, 0, extra=f)
    move_wipe(new[-1])
    new[-1]['steps'] = 40000000
    new[-1]['name'] = 'screen_body_2D (%s)'%nm

# ------------------------------------------- screen H'38, the pattern list
# The thumbnails: H'23 by H'23 one-bit bitmaps, five bytes to a row, H'AF
# bytes each. Row nought is not picture but a marker -- one set bit at the
# column the pattern is wide -- and the rows under it carry the pattern
# itself, in the columns to the left of that bit.
MOD_BITS = 0x00104D4A
MOD_LEN = 0x00AF
MOD_N = 0x17
MOD_WIDTHS_LIST = [0x0A, 0x14, 0x0E, 0x18, 0x10, 0x0C, 0x1A, 0x12]

def mod_bitmaps():
    d = collections.OrderedDict()
    # More bitmaps than there are patterns: the grid's three rows count what
    # is left three different ways, and a cell drawn from a pattern past the
    # last would look the same as one not drawn at all if there were nothing
    # there.
    d['104D4A:1600'] = '00'
    for k in range(0x20):
        w = MOD_WIDTHS_LIST[k % len(MOD_WIDTHS_LIST)]
        at = MOD_BITS + MOD_LEN*k
        rows = [[0]*5 for _ in range(0x23)]
        rows[0][w // 8] |= 0x80 >> (w % 8)
        for py in range(1, 0x23):
            for px in range(w):
                # A pattern that is not the same twice: a diagonal band and a
                # frame, so that a row drawn one place out is a difference.
                if px == 0 or px == w - 1 or py == 1 or py == 0x22 or \
                   ((px + py + k) % 5) == 0:
                    rows[py][px // 8] |= 0x80 >> (px % 8)
            # And a bit in the marker's own column now and then, so that a
            # limit one column out is a difference rather than nothing: the
            # columns from the marker on are never drawn, and with nothing
            # set in them the limit could be off by one unnoticed.
            if ((py + k) % 3) == 0:
                rows[py][w // 8] |= 0x80 >> (w % 8)
            if ((py + k) % 4) == 0 and w >= 2:
                rows[py][(w - 2) // 8] |= 0x80 >> ((w - 2) % 8)
        for py in range(0x23):
            for bi in range(5):
                if rows[py][bi]:
                    d.update(b8(at + 5*py + bi, rows[py][bi]))
    return d

MOD_SLOT = 0x02
MOD_DESIGN = 0x03

def pat_extra(extra=None):
    e = modscr_extra()
    e.update(block(0x00117FF0))
    e.update(mod_bitmaps())
    e['0F1610:2000'] = '00'
    e['104030:40'] = '00'
    # The three tables the send step puts back to nought: distinct non-zero
    # bytes, so that a word written one place along is a difference rather
    # than one zero for another.
    e['104C90:80'] = '00'
    e['104CC0:80'] = '00'
    e['104D00:40'] = '00'
    for _a in range(0x00104C90, 0x00104D40):
        e.update(b8(_a, ((_a * 0x35) & 0xFF) | 0x01))
    e['11F550:20'] = '00'
    e.update(b8(0x0011A660, MOD_SLOT))
    e.update(b8(0x0011A41A + 0x12*MOD_SLOT, MOD_DESIGN))
    e.update(b8(0x000FFE9C + MOD_DESIGN, MOD_N))
    e.update(b8(0x00114DB8, 0x00))       # the page the grid is showing
    e.update(b8(0x00104036, 0x0A))       # the width the last preview found
    e.update(b8(0x00104038, 0x28))       # how far along the strip
    e.update(b8(0x00104039, 0x03))       # which entry the cursor is on
    e.update(b8(0x00104044, 0x05))       # how many entries
    e.update(w16(0x0010403A, 0x0100))
    e.update(w16(0x0010403C, 0x0200))
    e.update(w16(0x0010403E, 0x0040))
    e.update(b8(0x00104040, 0x00))
    e.update(b8(0x00104041, 0x00))
    e.update(b8(0x00104042, 0x00))
    for k in range(1, 0x15):
        e.update(b8(0x00104045 + k, (k % MOD_N) + 1))
        e.update(b8(0x0010405A + k, MOD_WIDTHS_LIST[k % len(MOD_WIDTHS_LIST)]))
    e.update(w16(0x0011F55E, 0x1111))
    e.update(w16(0x0011F560, 0x2222))
    e.update(w16(0x0011F562, 0x0030))
    e.update(b8(0x0011F564, 0x28))
    e.update(b8(0x00114D99, 0x00))
    e.update(b8(0x00114D98, 0x01))
    e.update(b8(0x00114D9B, 0x00))
    e.update(b8(0x00114DA1, 0x00))
    e.update(b8(0x00114D7E, 0x00))
    e.update(b8(0x00114D7F, 0x00))
    e.update(b8(0x00114DAD, 0x00))
    e.update(b8(0x00114D8D, 0x00))
    e.update(b8(0x00114D8E, 0x00))
    e.update(b8(0x00114D93, 0x00))
    e.update(b8(0x00114D72, 0x00))
    e.update(b8(0x00114D73, 0x00))
    e.update(b8(0x00114DB9, 0x00))
    e.update(b8(0x0011F29E, 0x00))
    e.update(b8(0x0011F2B6, 0x00))
    e.update(b8(0x0011F2A1, 0x00))
    e.update(b8(0x00114D50, 0x00))
    # Both buffers given a pattern that changes from one line to the next:
    # the strip is scrolled by reading the front buffer back a pixel at a
    # time, and with every line the same a scroll of the wrong length would
    # put the same byte back.
    for row in range(0x00, 0xC8):
        e['%06X:50'%(0x00040000 + 0x50*row)] = '%02X'%((row*0x0B + 0x13) & 0xFF)
        e['%06X:50'%(0x00044B00 + 0x50*row)] = '%02X'%((row*0x1D + 0x47) & 0xFF)
    # And the strip's own rows given a byte that changes along the row as
    # well as down it. A row of one repeated byte is no use here: the strip
    # is scrolled *sideways*, so a shift of the wrong length would copy the
    # same value back over itself and leave nothing to compare.
    for row in range(0x9C, 0xC5):
        for col in range(0x0B, 0x3B):
            e.update(b8(0x00040000 + 0x50*row + col,
                        (row*0x35 + col*0x97 + 0x5B) & 0xFF))
    if extra: e.update(extra)
    return e

def pat_fill(extra=None):
    c = {'fill': base_fill(pat_extra(extra))}
    move_wipe(c)
    return c['fill']

def pat_add(name, orig, rebuilt, extra=None, steps=None):
    add(name, pat_fill(extra), orig, rebuilt, steps=steps)

# H'24A21A
for x0, y0, x1, y1, nm in ((0x0030, 0x00A0, 0x0060, 0x00B0, 'a plain one'),
                           (0x0000, 0x0000, 0x0004, 0x0004, 'the corner'),
                           (0x0060, 0x00A0, 0x0060, 0x00A0, 'a single pixel')):
    pat_add('module_box_clear (%s)'%nm,
            {'addr':'24A21A','regs':{'er6':'%04X'%x0},
             'stack':{'4':'2:%04X'%y0,'6':'2:%04X'%x1,'8':'2:%04X'%y1}},
            {'symbol':'_module_box_clear',
             'regs':{'er0':'%04X'%x0,'er1':'%04X'%y0,'er2':'%04X'%x1},
             'stack':{'4':'4:%08X'%y1}})

# H'249B86, H'249BD2, H'249C1E, H'249C6A
for a, sym in (('249B86','_module_page_arrow_back'),
               ('249BD2','_module_page_arrow_on'),
               ('249C1E','_module_list_arrow_on'),
               ('249C6A','_module_list_arrow_back')):
    for on, nm in ((0x01, 'lit'), (0x00, 'not lit')):
        pat_add('%s (%s)'%(sym[1:], nm),
                {'addr':a,'regs':{'er6':'%02X'%on}},
                {'symbol':sym,'regs':{'er0':'%02X'%on}})

# H'23A336
for row, first, count, nm in ((0x00, 0x0000, 0x05, 'the first row'),
                              (0x01, 0x0005, 0x05, 'the second'),
                              (0x02, 0x000A, 0x05, 'the third'),
                              (0x02, 0x0014, 0x03, 'a part row'),
                              (0x00, 0x0000, 0x00, 'nothing in it'),
                              (0x01, 0x0003, 0x02, 'two out of five')):
    pat_add('module_thumb_row_draw (%s)'%nm,
            {'addr':'23A336','regs':{'er6':'%02X'%row},
             'stack':{'4':'2:%04X'%first,'6':'2:%04X'%count}},
            {'symbol':'_module_thumb_row_draw',
             'regs':{'er0':'%02X'%row,'er1':'%04X'%first,'er2':'%02X'%count}},
            steps=40000000)

# H'23ABC2
for x, y, wide, k, nm in ((0x008A, 0x0003, 0x0001, 0, 'the preview'),
                          (0x008A, 0x0003, 0x0001, 3, 'the preview, a wide one'),
                          (0x002F, 0x009E, 0x0000, 0, 'the strip'),
                          (0x0060, 0x009E, 0x0000, 6, 'the strip, further along'),
                          (0x0060, 0x009E, 0x0000, 2, 'a narrow one')):
    pat_add('module_thumb_draw (%s)'%nm,
            {'addr':'23ABC2','regs':{'er6':'%04X'%x},
             'stack':{'4':'2:%04X'%y,'6':'2:%04X'%wide,
                      '8':'4:%08X'%(MOD_BITS + MOD_LEN*k)}},
            {'symbol':'_module_thumb_draw',
             'regs':{'er0':'%04X'%x,'er1':'%04X'%y,'er2':'%04X'%wide},
             'stack':{'4':'4:%08X'%(MOD_BITS + MOD_LEN*k)}},
            steps=40000000)

# H'23AFEE and H'23B0C6
for a, sym in (('23AFEE','_module_strip_scroll'),
               ('23B0C6','_module_strip_scroll_at')):
    for n, right, nm in ((0x0A, 0x00, 'left by ten'),
                         (0x0A, 0x01, 'right by ten'),
                         (0x01, 0x00, 'left by one'),
                         (0x01, 0x01, 'right by one'),
                         (0x00, 0x00, 'left by nothing'),
                         (0x00, 0x01, 'right by nothing'),
                         (0x40, 0x01, 'right by a lot')):
        pat_add('%s (%s)'%(sym[1:], nm),
                {'addr':a,'regs':{'er6':'%02X'%n},'stack':{'4':'2:%04X'%right}},
                {'symbol':sym,'regs':{'er0':'%02X'%n,'er1':'%02X'%right}},
                steps=60000000)

# H'23B81C
for on, ex, nm in ((0x00, None, 'forgotten'),
                   (0x01, b8(0x00114D99, 0x01), 'held off'),
                   (0x01, b8(0x0011F564, 0x10), 'moved'),
                   (0x01, w16(0x0011F562, 0x0064), 'the blink off'),
                   (0x01, w16(0x0011F562, 0x00C8), 'the blink on'),
                   (0x01, w16(0x0011F562, 0x00C9), 'the counter round'),
                   (0x01, None, 'between blinks')):
    pat_add('module_cursor_line (%s)'%nm,
            {'addr':'23B81C','regs':{'er6':'%02X'%on}},
            {'symbol':'_module_cursor_line','regs':{'er0':'%02X'%on}},
            extra=ex)

# H'23B408
for w, num, ex, nm in ((0x0E, 0x0007, None, 'in the middle'),
                       (0x0E, 0x0007, b8(0x00104039, 0x05), 'at the end'),
                       (0x0E, 0x0007, b8(0x00104039, 0x00), 'at the start'),
                       (0x18, 0x0001, b8(0x00104044, 0x00),
                        'the first of all')):
    pat_add('module_list_insert (%s)'%nm,
            {'addr':'23B408','regs':{'er6':'%02X'%w},
             'stack':{'4':'2:%04X'%num}},
            {'symbol':'_module_list_insert',
             'regs':{'er0':'%02X'%w,'er1':'%04X'%num}},
            extra=ex)

# H'23B4DE
for n, frm, nm in ((0x0E, 0x03, 'the middle one'),
                   (0x00, 0x03, 'nothing to move'),
                   (0x28, 0x02, 'a long way'),
                   (0x0E, 0x04, 'the last but one')):
    pat_add('module_list_remove_draw (%s)'%nm,
            {'addr':'23B4DE','regs':{'er6':'%02X'%n},
             'stack':{'4':'2:%04X'%frm}},
            {'symbol':'_module_list_remove_draw',
             'regs':{'er0':'%02X'%n,'er1':'%02X'%frm}},
            steps=60000000)

# The two edges of the redraw the close-up ends with: an entry whose right
# side lands exactly on where the blanking begins, and one whose right side
# lands exactly on the end of the strip. Every width the same, so that where
# an entry lands can be worked out rather than searched for.
_even = collections.OrderedDict()
for _k in range(1, 0x15):
    _even.update(b8(0x0010405A + _k, 0x10))
for at, nm in ((0x9D, 'an entry on the near edge'),
               (0xAB, 'an entry on the far edge')):
    pat_add('module_list_remove_draw (%s)'%nm,
            {'addr':'23B4DE','regs':{'er6':'0E'},'stack':{'4':'2:0001'}},
            {'symbol':'_module_list_remove_draw','regs':{'er0':'0E','er1':'01'}},
            extra=dict(list(_even.items()) + list(b8(0x00104038, at).items())),
            steps=60000000)

# H'23A990 and H'23AA8A
for a, sym in (('23A990','_module_thumb_page_back'),
               ('23AA8A','_module_thumb_page_on')):
    # H'08 and H'0D are the two that land on exactly fifteen left, which is
    # where both of the arrow decisions turn.
    #
    # The six page numbers are named for what they mean to the routine that
    # pages *back*; the routine that pages *on* only has somewhere to go
    # while there are more than fifteen thumbnails past the page it is
    # showing, and MOD_N is H'17. So its last four cases all turn back at
    # that guard and write nothing -- which is right, and one of the four is
    # the boundary itself, but the other three test what it already tested.
    # Its two remaining cases cover the walk and both of the arrow states.
    for page, nm in ((0x00, 'at the first page'), (0x05, 'a page along'),
                     (0x0A, 'two pages along'), (0x0F, 'near the end'),
                     (0x08, 'exactly fifteen left'),
                     (0x0D, 'a page back from exactly fifteen left')):
        pat_add('%s (%s)'%(sym[1:], nm),
                {'addr':a}, {'symbol':sym},
                extra=b8(0x00114DB8, page), steps=60000000)

# H'23B1A2, H'23B2CE and H'23B67A
for a, sym in (('23B1A2','_module_list_back'),
               ('23B2CE','_module_list_forward'),
               ('23B67A','_module_list_delete')):
    # H'18 is the width of the entry the cursor is on and H'AB is H'BB less
    # the width of the one after it: the two positions where the entry
    # exactly fills what is showing, which is where the test turns.
    for at, pos, nm in ((0x28, 0x03, 'in the middle'),
                        (0x00, 0x00, 'at the start'),
                        (0x00, 0x05, 'at the end with the strip home'),
                        (0xBB, 0x05, 'at the end'),
                        (0x04, 0x02, 'the strip nearly home'),
                        (0xB0, 0x04, 'the strip nearly full'),
                        (0x18, 0x03, 'the entry exactly back to the start'),
                        (0xAB, 0x03, 'the entry exactly filling the strip')):
        pat_add('%s (%s)'%(sym[1:], nm), {'addr':a}, {'symbol':sym},
                extra=dict(list(b8(0x00104038, at).items()) +
                           list(b8(0x00104039, pos).items())),
                steps=60000000)

# H'23AD2A
for box, ex, nm in ((0x01, None, 'the first box'),
                    (0x08, None, 'the eighth'),
                    (0x0F, None, 'the last of the fifteen'),
                    (0x01, b8(0x00104044, 0x14), 'the list already full'),
                    (0x01, dict(list(b8(0x00104044, 0x00).items()) +
                                list(b8(0x00104039, 0x00).items()) +
                                list(b8(0x00104038, 0x00).items())),
                     'the first of all'),
                    (0x01, dict(list(b8(0x00104039, 0x05).items()) +
                                list(b8(0x00104038, 0xB0).items())),
                     'at the end with the strip nearly full'),
                    (0x03, b8(0x00104038, 0xB8), 'the strip full'),
                    (0x01, b8(0x00114DB8, 0x14), 'past the last pattern'),
                    # Page H'14 and box four is pattern H'17, which is one
                    # past the last of the H'17 there are.
                    (0x04, b8(0x00114DB8, 0x14), 'one past the last pattern'),
                    (0x03, b8(0x00114DB8, 0x14), 'the last pattern of all'),
                    # H'B2 plus the width the preview records for the first
                    # pattern is exactly H'BB: the entry fills what is left
                    # of the strip and nothing over, which is where the test
                    # turns.
                    (0x01, dict(list(b8(0x00104038, 0xB2).items()) +
                                list(b8(0x00104039, 0x05).items())),
                     'exactly filling the strip at the end'),
                    (0x01, b8(0x00104038, 0xB2),
                     'exactly filling the strip in the middle')):
    pat_add('module_pattern_add (%s)'%nm,
            {'addr':'23AD2A','regs':{'er6':'%02X'%box}},
            {'symbol':'_module_pattern_add','regs':{'er0':'%02X'%box}},
            extra=ex, steps=60000000)

# H'23B938
for st, ex, nm in ((0x00, None, 'the list to send'),
                   (0x00, b8(0x00104044, 0x00), 'nothing to send'),
                   (0x00, dict(list(b8(0x00104044, 0x00).items()) +
                               list(b8(0x00114DA1, 0x01).items())),
                    'nothing to send, the stitch screen'),
                   (0x00, dict(list(b8(0x00104044, 0x00).items()) +
                               list(b8(0x00114D9B, 0x01).items())),
                    'nothing to send, already reported'),
                   (0x01, None, 'the first message'),
                   (0x01, b8(0x0011F29E, 0x01), 'the link still busy'),
                   (0x02, None, 'the wait'),
                   (0x03, None, 'the defaults'),
                   (0x04, None, 'the hoop asked about'),
                   (0x05, None, 'the last step'),
                   (0x06, None, 'a step there is none of')):
    pat_add('module_send_step (%s)'%nm, {'addr':'23B938'},
            {'symbol':'_module_send_step'},
            extra=dict(list(b8(0x00104042, st).items()) +
                       (list(ex.items()) if ex else [])), steps=60000000)

# H'2309EC
def modbox(b):
    return (0x0C + 0x10*((b - 1) % 12), 0x24 + 0x10*((b - 1) // 12))

pat_add('module_pattern_screen (nothing pressed)',
        {'addr':'2309EC'}, {'symbol':'_module_pattern_screen'})
for b, nm in ((0x01, 'the first thumbnail'), (0x0F, 'the fifteenth'),
              (0x10, 'a page back'), (0x11, 'a page on'),
              (0x12, 'the strip back'), (0x13, 'the strip on'),
              (0x14, 'the delete'), (0x15, 'the send')):
    bx, by = modbox(b)
    pat_add('module_pattern_screen (%s)'%nm,
            {'addr':'2309EC'}, {'symbol':'_module_pattern_screen'},
            extra=dict(list(b8(0x00FFFED9, bx).items()) +
                       list(b8(0x00FFFEDA, by).items()) +
                       list(boxval(b, b).items())), steps=60000000)
# A box whose value is not its own number, so that the message held is told
# from the value pressed; and a value one past the last the table covers.
pat_add('module_pattern_screen (a box that is not its own number)',
        {'addr':'2309EC'}, {'symbol':'_module_pattern_screen'},
        extra=dict(list(b8(0x00FFFED9, 0x5C).items()) +
                   list(b8(0x00FFFEDA, 0x24).items()) +
                   list(boxval(6, 0x0010).items())), steps=60000000)
pat_add('module_pattern_screen (a value past the table)',
        {'addr':'2309EC'}, {'symbol':'_module_pattern_screen'},
        extra=dict(list(b8(0x00FFFED9, 0x0C).items()) +
                   list(b8(0x00FFFEDA, 0x24).items()) +
                   list(boxval(1, 0x0016).items())), steps=60000000)
pat_add('module_pattern_screen (the module busy)',
        {'addr':'2309EC'}, {'symbol':'_module_pattern_screen'},
        extra=dict(list(b8(0x00FFFED9, 0x0C).items()) +
                   list(b8(0x00FFFEDA, 0x24).items()) +
                   list(boxval(1, 1).items()) +
                   list(b8(0x00114D7F, 0x01).items())))

# H'225B8A
for nm, arrived, relayout, ex in (('just arrived', 1, 0, None),
                                  ('laid out again', 0, 1, None),
                                  ('a plain pass', 0, 0, None),
                                  ('just arrived, the table pointer moved',
                                   1, 0, w32(0x0011B0BA, 0x000E7800)),
                                  ('a press on the first thumbnail', 0, 0,
                                   dict(list(b8(0x00FFFED9, 0x0C).items()) +
                                        list(b8(0x00FFFEDA, 0x24).items()) +
                                        list(boxval(1, 1).items())))):
    e = pat_extra(ex)
    dispatch_case(nm, 0x38, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['steps'] = 60000000
    new[-1]['name'] = 'screen_body_38 (%s)'%nm

# ------------------------------------------- the module screens' furniture
STR_AT = 0x000E6900

def furn_extra(extra=None):
    e = pat_extra()
    e['0ECB10:4B00'] = '55'
    e['0E6900:20'] = '00'
    # Digits and 'm' only: those are the glyphs the test font has, and a
    # character it has no glyph for reads its pointer as nought and draws a
    # bitmap whose header comes from address zero.
    for _i, _c in enumerate('17m'):
        e.update(b8(STR_AT + _i, ord(_c)))
    e['1040A0:20'] = '00'
    e.update(b8(0x001040AE, 0x06))
    e.update(b8(0x001040AF, 0x0A))
    e.update(b8(0x001040B0, 0x00))
    e.update(b8(0x001040B1, 0x01))
    e.update(b8(0x001040B9, 0x00))
    e['11A250:20'] = '00'
    e.update(b8(0x00114D51, 0x00))
    e.update(b8(0x00114D53, 0x00))
    e.update(w16(0x00114D4C, 0x0000))
    e.update(w16(0x0011B318, 0x0007))
    e.update(b8(0x0011F4E6, 0x00))
    # Glyphs for 'o' and 'f' as well, pointed at the one the test font has
    # for 'm': the label that says "off" draws those two, and a character
    # with no glyph reads its bitmap from address zero.
    # Glyphs for 'o', 'f' and '%' as well: the label that says "off" draws
    # the first two and the percentage draws the third.
    # A different glyph for each, so that one letter cannot be mistaken for
    # another: 'o' takes the font's 'm' and the rest take digits. Both copies
    # of the font get them -- H'119A66 is the one the left-hand labels use and
    # H'1196EA the one the count label does. Every letter any of these
    # routines draws has to be here: a character with no glyph reads its
    # bitmap header from address zero and paints a column down the screen.
    for _ch, _g in ((0x6F, 0x000E1640), (0x66, 0x000E1500),
                    (0x25, 0x000E1520), (0x67, 0x000E1540),
                    (0x69, 0x000E1560), (0x6E, 0x000E1580),
                    (0x6D, 0x000E1640)):
        e.update(w32(0x00119A66 + 4*_ch - 0x84, _g))
        e.update(w32(0x001196EA + 4*_ch - 0x84, _g))
    e['11F2D0:20'] = '00'
    e['11F290:08'] = '00'
    # The box table's own origin moved down clear of the bar. Left where the
    # boot has it the second row of boxes falls off the bottom of the buffer;
    # put at the corner it lands under the percentage bar, which is drawn
    # afterwards and paints over it. Neither can be seen, and a box the
    # colour strip did not draw would look the same as one it did.
    e.update(w16(0x0011B0B2, 0x0000))
    e.update(w16(0x0011B0B4, 0x0050))
    e.update(b8(0x00114D56, 0x20))
    e.update(b8(0x00114DB9, 0x00))
    e.update(b8(0x00114D88, 0x00))
    e['11F530:10'] = '00'
    e.update(b8(0x0011F534, 0x00))
    e.update(b8(0x0011F538, 0x00))
    if extra: e.update(extra)
    return e

def furn_fill(extra=None):
    c = {'fill': base_fill(furn_extra(extra))}
    move_wipe(c)
    return c['fill']

def furn(name, orig, rebuilt, extra=None, steps=None, drop=None, repin=None,
         pin=None):
    f = furn_fill(extra)
    # Narrow pins a wide key added later would shadow, taken out rather than
    # left to be covered; and pins that have to sit after such a key, moved
    # to the end so they survive it.
    for lo, hi in (drop or ()):
        for k in [k for k in f
                  if int(k.split(':')[1], 16) == 1 and
                     lo <= int(k.split(':')[0], 16) < hi]:
            del f[k]
    for a in (repin or ()):
        k = '%06X:1' % a
        if k in f:
            v = f.pop(k)
            f[k] = v
    # Values that have to survive a wide key added in the same fill: put on
    # the very end, after the drop has taken the shadowed ones out.
    for k, v in (pin or {}).items():
        f.pop(k, None)
        f[k] = v
    add(name, f, orig, rebuilt, steps=steps)

# H'241480
furn('stream_clear', {'addr':'241480'}, {'symbol':'_stream_clear'},
     extra={'10C27A:7530':'A5'}, steps=200000000)

# H'2172B6 and the five beside it
for a, sym in (('2172B6','_module_label_right_top'),
               ('2173E6','_module_label_right_mid'),
               ('217432','_module_label_right_low'),
               ('217302','_module_label_right_foot'),
               ('21734E','_module_label_mid_top'),
               ('21739A','_module_label_mid_second')):
    furn(sym[1:], {'addr':a,'regs':{'er6':'%X'%STR_AT}},
         {'symbol':sym,'regs':{'er0':'%X'%STR_AT}}, steps=40000000)

# H'244CA2
for v, nm in ((0x00, 'nothing to report'), (0x40, 'something to report')):
    furn('module_nothing_to_report (%s)'%nm,
         {'addr':'244CA2','result':'r6l'},
         {'symbol':'_module_nothing_to_report','result':'r0l'},
         extra=b8(0x00114D51, v))

# H'244CB4
for d53, d51, d4c, nm in ((0x50, 0x00, 0x0000, 'the first kind'),
                          (0x50, 0x00, 0x4000, 'the first kind, the bit up'),
                          (0x40, 0x00, 0x0000, 'the first kind without its bit'),
                          (0x60, 0x02, 0x0000, 'the second kind'),
                          (0x60, 0x00, 0x0000, 'the second kind, not allowed'),
                          (0x20, 0x02, 0x0000, 'neither kind')):
    furn('module_hoop_sewable (%s)'%nm,
         {'addr':'244CB4','result':'r6l'},
         {'symbol':'_module_hoop_sewable','result':'r0l'},
         extra=dict(list(b8(0x00114D53, d53).items()) +
                    list(b8(0x00114D51, d51).items()) +
                    list(w16(0x00114D4C, d4c).items())))

# H'24654E
def slotbytes(a, b, c, d, slot=MOD_SLOT):
    e = 0x0011A25A + 0x10*slot
    return dict(list(b8(e + 0, a).items()) + list(b8(e + 1, b).items()) +
                list(b8(e + 5, c).items()) + list(b8(e + 6, d).items()))
for vals, nm in (((0x32, 0x32, 0x00, 0x24), 'the plain one'),
                 ((0x30, 0x32, 0x00, 0x24), 'the first byte wrong'),
                 ((0x32, 0x30, 0x00, 0x24), 'the second wrong'),
                 ((0x32, 0x32, 0x01, 0x24), 'the third wrong'),
                 ((0x32, 0x32, 0x00, 0x25), 'the fourth wrong')):
    furn('module_slot_is_plain (%s)'%nm,
         {'addr':'24654E','result':'r6l'},
         {'symbol':'_module_slot_is_plain','result':'r0l'},
         extra=slotbytes(*vals))

# H'23C570
for asked, which, nm in ((0x08, 0x01, 'inside the count'),
                         (0x04, 0x01, 'the first colour'),
                         (0x0A, 0x01, 'exactly the count'),
                         (0x0B, 0x01, 'one past the count'),
                         (0x0C, 0x01, 'two past the count'),
                         (0x14, 0x01, 'past the fifteenth'),
                         (0x13, 0x01, 'the fifteenth'),
                         (0x08, 0x00, 'the other count'),
                         (0x00, 0x01, 'below the first')):
    furn('module_colour_check (%s)'%nm,
         {'addr':'23C570','result':'r6l','regs':{'er6':'%02X'%asked}},
         {'symbol':'_module_colour_check','result':'r0l',
          'regs':{'er0':'%02X'%asked}},
         extra=b8(0x001040B1, which))
# A count wide enough that the H'0F test is the only one that stops it.
furn('module_colour_check (past the fifteenth, a long pattern)',
     {'addr':'23C570','result':'r6l','regs':{'er6':'14'}},
     {'symbol':'_module_colour_check','result':'r0l','regs':{'er0':'14'}},
     extra=dict(list(b8(0x001040B1, 0x01).items()) +
                list(b8(0x001040AE, 0x10).items())))

# H'24A1EA
for x, y, c, nm in ((0x0040, 0x0060, 0x03, 'a pixel'),
                    (0x0000, 0x0000, 0x01, 'the corner')):
    furn('plot_pixel_back (%s)'%nm,
         {'addr':'24A1EA','regs':{'er6':'%04X'%x},
          'stack':{'4':'2:%04X'%y,'6':'2:%04X'%c}},
         {'symbol':'_plot_pixel_back',
          'regs':{'er0':'%04X'%x,'er1':'%04X'%y,'er2':'%02X'%c}})

# H'249FC2 and H'249FFE
for a, sym in (('249FC2','_module_area_clear_front'),
               ('249FFE','_module_area_clear_back')):
    furn(sym[1:], {'addr':a}, {'symbol':sym}, steps=60000000)

# H'24A25A and H'24A29A
for a, sym in (('24A25A','_module_box_clear_back'),
               ('24A29A','_module_box_outline')):
    for x0, y0, x1, y1, nm in ((0x0030, 0x0060, 0x0060, 0x0080, 'a plain one'),
                               (0x0000, 0x0000, 0x0004, 0x0004, 'the corner')):
        furn('%s (%s)'%(sym[1:], nm),
             {'addr':a,'regs':{'er6':'%04X'%x0},
              'stack':{'4':'2:%04X'%y0,'6':'2:%04X'%x1,'8':'2:%04X'%y1}},
             {'symbol':sym,
              'regs':{'er0':'%04X'%x0,'er1':'%04X'%y0,'er2':'%04X'%x1},
              'stack':{'4':'4:%08X'%y1}})

# H'24A336
for b, nm in ((0x01, 'the first box'), (0x07, 'the seventh')):
    furn('hitbox_repress (%s)'%nm,
         {'addr':'24A336','regs':{'er6':'%02X'%b}},
         {'symbol':'_hitbox_repress','regs':{'er0':'%02X'%b}}, steps=40000000)

# H'232394
for b, nm in ((0x04, 'below the strip'), (0x05, 'the first of the strip'),
              (0x13, 'the last of it'), (0x14, 'past the strip'),
              (0x09, 'in the middle')):
    furn('module_strip_press (%s)'%nm,
         {'addr':'232394','regs':{'er6':'%02X'%b}},
         {'symbol':'_module_strip_press','regs':{'er0':'%02X'%b}},
         steps=40000000)

# H'2317D0 and H'2317EE
for a, sym in (('2317D0','_module_go_check'), ('2317EE','_module_go_report')):
    furn(sym[1:], {'addr':a}, {'symbol':sym})

# H'231544
for v, nm in ((0x00, 'nothing kept yet'), (0x01, 'put back')):
    furn('module_area_restore (%s)'%nm, {'addr':'231544'},
         {'symbol':'_module_area_restore'},
         extra=b8(0x0011F4E6, v), steps=60000000)

# H'217A26
for on, kind, nm in ((0x01, 0x00, 'lit'), (0x01, 0x01, 'lit again'),
                     (0x00, 0x01, 'put back'), (0x00, 0x00, 'already back')):
    furn('module_box4_press (%s)'%nm,
         {'addr':'217A26','regs':{'er6':'%02X'%on}},
         {'symbol':'_module_box4_press','regs':{'er0':'%02X'%on}},
         extra=b8(PLAIN_TABLE + 0x12*4 + 0x10, kind), steps=40000000)

# H'217A82
for three, nm in ((0x01, 'three'), (0x00, 'four')):
    furn('module_box34_pick (%s)'%nm,
         {'addr':'217A82','regs':{'er6':'%02X'%three}},
         {'symbol':'_module_box34_pick','regs':{'er0':'%02X'%three}},
         steps=40000000)

# H'217C32
for box, ex, nm in ((0x0009, None, 'a box along'),
                    (0x0007, None, 'the one already lit'),
                    (0x0009, b8(PLAIN_TABLE + 0x12*9 + 0x10, 0x01),
                     'a box of the lit kind'),
                    (0x0001, None, 'the first box')):
    furn('module_lit_box (%s)'%nm,
         {'addr':'217C32','regs':{'er6':'%04X'%box}},
         {'symbol':'_module_lit_box','regs':{'er0':'%04X'%box}}, steps=40000000)

# H'2323AA and H'2323F0
for v, nm in ((0x00, 'greyed'), (0x02, 'left alone')):
    furn('module_box3_grey (%s)'%nm, {'addr':'2323AA'},
         {'symbol':'_module_box3_grey'}, extra=b8(0x00114D51, v),
         steps=40000000)
for v, nm in ((0x00, 'greyed'), (0x01, 'left alone')):
    furn('module_boxA_grey (%s)'%nm, {'addr':'2323F0'},
         {'symbol':'_module_boxA_grey'}, extra=b8(0x0011F538, v),
         steps=40000000)

# H'2352AA
for v, nm in ((0xFF, 'the bit up'), (0x00, 'the bit already down')):
    furn('link_line_release (%s)'%nm, {'addr':'2352AA'},
         {'symbol':'_link_line_release'}, extra=b8(0x00FFFD1C, v))

# ------------------------------------------- the second layer of the module
# H'230EA8
for v, nm in ((0x00, 'nothing to put away yet'), (0x01, 'put away')):
    furn('module_area_save (%s)'%nm, {'addr':'230EA8'},
         {'symbol':'_module_area_save'}, extra=b8(0x0011F4E6, v),
         steps=60000000)

# H'2316C4. The path that sends a message ends in a wait only the link's own
# interrupt can finish, so the two that do not send are the ones covered.
for ex, nm in ((b8(0x00114D51, 0x00), 'nothing to tell it'),
               (dict(list(b8(0x00114D51, 0x40).items()) +
                     list(b8(0x00114D50, 0x01).items())), 'the link busy'),
               (dict(list(b8(0x00114D51, 0x40).items()) +
                     list(b8(0x0011F29E, 0x01).items())), 'a message going out'),
               (b8(0x00114D51, 0x20), 'the bit beside it')):
    furn('module_go_settings (%s)'%nm, {'addr':'2316C4'},
         {'symbol':'_module_go_settings'}, extra=ex)

# H'2321B6
_e16 = 0x0011A25A + 0x10*MOD_SLOT
for v, nm in ((0x00, 'off'), (0x28, 'eight'), (0x64, 'twenty'), (0x05, 'one')):
    furn('module_label_speed (%s)'%nm,
         {'addr':'2321B6'}, {'symbol':'_module_label_speed'},
         extra=b8(_e16 + 0x0A, v), steps=60000000)

# H'23C5EA
for ex, nm in ((None, 'the middle of a pattern'),
               (b8(0x00114DB9, 0x01), 'the module reporting'),
               (b8(0x001040AE, 0x02), 'two colours'),
               (b8(0x001040AE, 0x10), 'more than fifteen'),
               (b8(0x001040AE, 0x0F), 'exactly fifteen'),
               (b8(0x001040B0, 0x00), 'the first colour on'),
               (b8(0x00114D51, 0x00), 'neither bit'),
               (b8(0x00114D51, 0x02), 'the second bit'),
               (dict(list(b8(0x00114D51, 0x01).items()) +
                     list(b8(0x00114D56, 0x60).items())),
                'the module further on than the count'),
               (b8(0x001040B1, 0x00), 'the other count')):
    e = dict(list(b8(0x00114D51, 0x01).items()) +
             list(b8(0x001040B0, 0x04).items()))
    if ex: e.update(ex)
    furn('module_colours_show (%s)'%nm, {'addr':'23C5EA'},
         {'symbol':'_module_colours_show'}, extra=e, steps=60000000,
         repin=(0x00114DB9,))

# H'246654
for st, code, ex, nm in ((0x04, 0x80, b8(0x00114D51, 0x40), 'a fault to report'),
                         (0x04, 0x80, b8(0x00114D51, 0x00), 'nothing to report'),
                         (0x08, 0x80, b8(0x00114D51, 0x40), 'from the eighth state'),
                         (0x09, 0x80, b8(0x00114D51, 0x40), 'from the ninth'),
                         (0x0A, 0x80, b8(0x00114D51, 0x40), 'from the tenth'),
                         (0x07, 0x83, None, 'the seventh state and its own code'),
                         (0x07, 0x80, b8(0x00114D51, 0x40), 'the seventh state, another code'),
                         (0x03, 0x80, b8(0x00114D51, 0x40), 'a state that does not listen'),
                         (0x04, 0x81, b8(0x00114D51, 0x40), 'the stop asked for'),
                         (0x04, 0x81, dict(list(b8(0x00114D51, 0x40).items()) +
                                           list(b8(0x00114DA1, 0x01).items())),
                          'the stop asked for, the stitch screen'),
                         (0x04, 0x82, b8(0x0011F534, 0x01), 'the stop taken'),
                         (0x04, 0x82, b8(0x0011F534, 0x00), 'no stop to take'),
                         (0x04, 0x84, b8(0x0011F534, 0x01), 'the stop taken, the other way'),
                         (0x04, 0x84, dict(list(b8(0x0011F534, 0x01).items()) +
                                           list(b8(0x00114DA1, 0x01).items())),
                          'the stop taken, the stitch screen'),
                         (0x04, 0x85, None, 'a code nothing claims')):
    e = dict(list(b8(0x00114D8E, st).items()))
    if ex: e.update(ex)
    furn('module_fault_report (%s)'%nm,
         {'addr':'246654','result':'r6l','regs':{'er6':'%02X'%code}},
         {'symbol':'_module_fault_report','result':'r0l',
          'regs':{'er0':'%02X'%code}}, extra=e)

# H'2414AE
furn('module_slots_clear', {'addr':'2414AE'}, {'symbol':'_module_slots_clear'},
     extra=dict(list({'10C27A:7530':'A5', '11A250:500':'D2'}.items()) +
                list(w16(0x00114D4C, 0xFFFF).items()) +
                list(w16(0x0011F4E8, 0x0000).items()) +
                list(w16(0x0011F4EA, 0x0000).items())),
     drop=((0x0011A250, 0x0011A750),), steps=200000000)

# ------------------------------------------- the colour and the hoop's frame
def swatch_bits():
    d = collections.OrderedDict()
    d['10032E:472'] = '00'
    for a in range(0x0010032E, 0x0010032E + 0x472):
        d.update(b8(a, ((a * 0x35) ^ (a >> 3)) & 0xFF))
    return d

CORNERS = collections.OrderedDict()
CORNERS.update(w16(0x0011F2E4, 0x0030))
CORNERS.update(w16(0x0011F2E6, 0x0080))
CORNERS.update(w16(0x0011F2E8, 0x0040))
CORNERS.update(w16(0x0011F2EA, 0x0050))
CORNERS.update(w16(0x0011F2EE, 0x0090))
CORNERS.update(w16(0x0011F2F0, 0x0020))
CORNERS.update(w16(0x0011F2F2, 0x0070))
CORNERS.update(w16(0x0011F2F4, 0x0060))

def mod2(name, orig, rebuilt, extra=None, steps=None, repin=None, drop=None,
         pin=None):
    e = collections.OrderedDict(CORNERS)
    e.update(b8(0x001040B5, 0x00))
    e.update(b8(0x001040B6, 0x00))
    e.update(b8(0x001040B7, 0x00))
    e.update(b8(0x0011F571, 0x00))
    e.update(b8(0x0011A61B, 0x00))
    e.update(b8(0x0011F2A2, 0x00))
    if extra: e.update(extra)
    furn(name, orig, rebuilt, extra=e, steps=steps, repin=repin, drop=drop,
         pin=pin)

# H'24A03A and H'24A112
for a, sym in (('24A03A','_module_frame_front'), ('24A112','_module_frame_back')):
    for c, nm in ((0x03, 'lit'), (0x00, 'blanked')):
        mod2('%s (%s)'%(sym[1:], nm),
             {'addr':a,'regs':{'er6':'%02X'%c}},
             {'symbol':sym,'regs':{'er0':'%02X'%c}}, steps=60000000)

# H'23E026
for n, nm in ((0x01, 'the first colour'), (0x02, 'the second'),
              (0x03, 'the third')):
    mod2('module_colour_swatch (%s)'%nm, {'addr':'23E026'},
         {'symbol':'_module_colour_swatch'},
         extra=dict(list(swatch_bits().items()) +
                    list(b8(0x001040B0, n).items())), steps=90000000)

# H'23DE8E
for st, code, ex, nm in ((0x00, 0x09, None, 'the colour drawn'),
                         (0x00, 0x09, b8(0x00114D53, 0x00), 'the hoop not sewable'),
                         (0x00, 0x09, b8(0x001040AE, 0x00), 'no colours'),
                         (0x00, 0x09, b8(0x001040B0, 0x08), 'past the last colour'),
                         (0x00, 0x09, b8(0x001040B0, 0x06), 'exactly the last colour'),
                         (0x00, 0x0A, None, 'a step on'),
                         (0x00, 0x0A, b8(0x00114D53, 0x00),
                          'a step on, the hoop not sewable'),
                         (0x00, 0x05, None, 'back to the start'),
                         (0x01, 0x00, None, 'the first message'),
                         (0x01, 0x00, b8(0x0011F29E, 0x01), 'the link busy'),
                         (0x02, 0x00, None, 'the second message'),
                         (0x03, 0x00, None, 'the last step'),
                         (0x07, 0x00, None, 'a step past the last')):
    e = dict(list(b8(0x001040B6, st).items()) +
             list(b8(0x00114D53, 0x50).items()) +
             list(b8(0x001040B0, 0x02).items()) +
             list(swatch_bits().items()))
    if ex: e.update(ex)
    mod2('module_start_step (%s)'%nm,
         {'addr':'23DE8E','regs':{'er6':'%02X'%code}},
         {'symbol':'_module_start_step','regs':{'er0':'%02X'%code}},
         extra=e, steps=90000000)

# H'23C450 and H'23C2FA. Once a message has gone out the wait that follows
# can only be ended by the link's own interrupt, so the cases are the ways
# these two turn back.
for ex, nm in ((b8(0x00114D51, 0x00), 'the hoop not there'),
               (dict(list(b8(0x00114D51, 0x02).items()) +
                     list(b8(0x001040B1, 0x04).items())), 'already measured')):
    mod2('module_measure_second (%s)'%nm, {'addr':'23C450'},
         {'symbol':'_module_measure_second'}, extra=ex)
for ex, nm in ((b8(0x00114D51, 0x00), 'the hoop not there'),
               (dict(list(b8(0x00114D51, 0x02).items()) +
                     list(b8(0x001040B1, 0x01).items())), 'already measured'),
               (dict(list(b8(0x00114D51, 0x02).items()) +
                     list(b8(0x001040B1, 0x04).items())), 'nothing attached')):
    mod2('module_measure_first (%s)'%nm, {'addr':'23C2FA'},
         {'symbol':'_module_measure_first'}, extra=ex)

# ------------------------------------------- the labels and the whole grid
# H'231450 and H'23128C
for a, sym in (('231450','_module_size_show'), ('23128C','_module_scale_show')):
    for x, y, nm in ((0x0064, 0x0032, 'a plain one'),
                     (0x0000, 0x0000, 'both nought'),
                     (0x00C8, 0x00C8, 'both at the top')):
        mod2('%s (%s)'%(sym[1:], nm),
             {'addr':a,'regs':{'er6':'%04X'%x},'stack':{'4':'2:%04X'%y}},
             {'symbol':sym,'regs':{'er0':'%04X'%x,'er1':'%04X'%y}},
             extra=dict(list(w16(0x0011F292, 0x0087).items()) +
                        list(slotbytes(0x32, 0x28, 0x00, 0x24).items()) +
                        list(b8(0x0011A25A + 0x10*MOD_SLOT + 0x02, 0x14).items()) +
                        list(b8(0x0011A25A + 0x10*MOD_SLOT + 0x07, 0x1E).items()) +
                        list(b8(0x0011F4E6, 0x01).items())),
             steps=90000000)

# H'2321B4
furn('module_label_hook', {'addr':'2321B4'}, {'symbol':'_module_label_hook'})

# H'23A7B0
for page, ex, nm in ((0x00, None, 'the first page'),
                     (0x05, None, 'a page along'),
                     (0x0F, None, 'near the end'),
                     (0x13, None, 'at the last page'),
                     (0x14, None, 'past the last page'),
                     (0x0D, None, 'the third row part full'),
                     (0x08, None, 'exactly sixteen left'),
                     (0x00, b8(0x00104039, 0x00), 'the strip at its start'),
                     (0x00, b8(0x00104039, 0x05), 'the strip at its end')):
    e = dict(list(b8(0x00114DB8, page).items()))
    if ex: e.update(ex)
    mod2('module_grid_draw (%s)'%nm, {'addr':'23A7B0'},
         {'symbol':'_module_grid_draw'}, extra=e, steps=200000000)

# ------------------------------------------- the hoop pictures and outline
# H'2369C4
for a16, b16, sa, sb, nm in ((0x0190, 0x01F4, 0x64, 0x64, 'a small design'),
                             (0x0400, 0x0500, 0x64, 0x64, 'a big one'),
                             (0x0190, 0x01F4, 0xC8, 0xC8, 'scaled up'),
                             (0x0000, 0x0000, 0x64, 0x64, 'nothing at all'),
                             (0x0300, 0x0200, 0x64, 0x64, 'wide but not tall'),
                             (0x0200, 0x0400, 0x64, 0x64, 'tall but not wide'),
                             # With both scales at fifty the two measurements
                             # come out as the table words themselves, so a
                             # limit can be landed on exactly.
                             (0x060F, 0x0064, 0x32, 0x32, 'one past the first limit'),
                             (0x060E, 0x0064, 0x32, 0x32, 'exactly the first limit'),
                             (0x0064, 0x07D1, 0x32, 0x32, 'one past the first limit down'),
                             (0x03E9, 0x0064, 0x32, 0x32, 'one past the second limit'),
                             (0x0064, 0x0515, 0x32, 0x32, 'one past the second limit down'),
                             (0x0259, 0x0064, 0x32, 0x32, 'one past the third limit'),
                             (0x0064, 0x0191, 0x32, 0x32, 'one past the third limit down'),
                             (0x0190, 0x0032, 0x32, 0x64, 'the two scales apart')):
    mod2('module_hoop_pictures (%s)'%nm, {'addr':'2369C4'},
         {'symbol':'_module_hoop_pictures'},
         extra=dict(list(b8(0x0011A41A + 0x12*MOD_SLOT, MOD_DESIGN).items()) +
                    list(w16(0x00104CCE + 2*MOD_DESIGN, a16).items()) +
                    list(w16(0x00104D06 + 2*MOD_DESIGN, b16).items()) +
                    list(b8(0x0011A25A + 0x10*MOD_SLOT, sa).items()) +
                    list(b8(0x0011A25B + 0x10*MOD_SLOT, sb).items()) +
                    list(b8(0x0011A612, 0x00).items())),
         steps=90000000)

# H'236BE4
for w, h, kind, nm in ((0x03E8, 0x04B0, 0x00, 'the plain hoop'),
                       (0x03E8, 0x04B0, 0x07, 'the one with the double line'),
                       (0x0064, 0x0064, 0x00, 'a small one'),
                       (0x0000, 0x0000, 0x00, 'nothing at all'),
                       (0x07D0, 0x0898, 0x07, 'a big one, doubled')):
    mod2('module_hoop_outline (%s)'%nm, {'addr':'236BE4'},
         {'symbol':'_module_hoop_outline'},
         extra=dict(list(w16(0x0011A626, w).items()) +
                    list(w16(0x0011A628, h).items()) +
                    list(b8(0x00114D4E, kind).items())),
         steps=90000000)

# ------------------------------------------- the thirteen fetch steps
# H'242868. Every step but the first sends a message and then waits, so a
# case can only ever see one step: the fill sets the step and the link is
# quiet, and the answer is what that one step did.
for st, which, ex, nm in (
        (0x00, 0x0001, None, 'the stitch screen asking'),
        (0x00, 0x0002, None, 'the other screen asking'),
        (0x00, 0x0001, b8(0x00114D51, 0x00), 'nothing to fetch'),
        (0x00, 0x0001, b8(0x0011A63C, 0x00), 'neither bit set'),
        (0x00, 0x0001, b8(0x0011A63C, 0x08), 'the other bit'),
        (0x01, 0x0001, None, 'the slate cleared'),
        (0x01, 0x0002, w16(0x0011F58C, 0x0002), 'the slate cleared, the other way'),
        (0x01, 0x0001, dict(list(b8(0x00114D51, 0x02).items()) +
                            list(b8(0x001040B1, 0x01).items())),
         'the hoop already measured'),
        (0x01, 0x0001, dict(list(b8(0x00114D51, 0x02).items()) +
                            list(b8(0x001040B1, 0x04).items())),
         'the hoop measured the other way'),
        (0x01, 0x0001, b8(0x0011F58E, 0x08), 'the second bit'),
        (0x02, 0x0001, None, 'the first message'),
        (0x02, 0x0001, b8(0x0011F29E, 0x01), 'the link busy'),
        (0x03, 0x0001, None, 'the second message'),
        (0x04, 0x0001, None, 'the third'),
        (0x05, 0x0001, None, 'the fourth'),
        (0x06, 0x0001, None, 'the fifth'),
        (0x07, 0x0001, None, 'the design number taken'),
        (0x07, 0x0001, b8(0x000FFE80, 0x40), 'a design on the third page'),
        (0x07, 0x0001, b8(0x000FFE80, 0x1B), 'a design on the second page'),
        (0x07, 0x0001, b8(0x000FFE80, 0x1A), 'the last of the first page'),
        (0x07, 0x0001, b8(0x000FFE80, 0x1C), 'the first of the second page'),
        (0x08, 0x0001, None, 'the step that falls through'),
        (0x09, 0x0001, None, 'the ninth'),
        (0x0A, 0x0001, None, 'the tenth'),
        (0x0B, 0x0001, None, 'the count taken'),
        (0x0C, 0x0001, None, 'the last step'),
        (0x0C, 0x0002, w16(0x0011F58C, 0x0002), 'the last step, the other way'),
        (0x0C, 0x0001, b8(0x0011F29E, 0x01), 'the last step, the link busy'),
        (0x0D, 0x0001, None, 'a step past the last')):
    e = dict(list(b8(0x0011A63D, st).items()) +
             list(b8(0x00114D51, 0x40).items()) +
             list(b8(0x0011A63C, 0x04).items()) +
             list(b8(0x0011F58E, 0x04).items()) +
             list(w16(0x0011F58C, 0x0001).items()) +
             list(b8(0x0011F58F, 0x00).items()) +
             list(b8(0x000FFE80, 0x05).items()) +
             list(b8(0x00100255, 0x02).items()) +
             list({'11A61E:22':'A7', '11F580:20':'B3',
                   '104C90:200':'C5', '114D60:40':'D7'}.items()))
    if ex: e.update(ex)
    keep = collections.OrderedDict()
    keep.update(b8(0x0011A63C, 0x04))
    keep.update(b8(0x0011A63D, st))
    keep.update(w16(0x0011F58C, 0x0001))
    keep.update(b8(0x0011F58E, 0x04))
    keep.update(b8(0x0011F58F, 0x00))
    keep.update(b8(0x00114D72, 0x00))
    keep.update(b8(0x00114D73, 0x00))
    keep.update(b8(0x00114D99, 0x00))
    keep.update(b8(0x000FFE80, 0x05))
    if ex:
        for _k in list(ex):
            if _k in keep: keep[_k] = ex[_k]
    mod2('module_run_fetch (%s)'%nm,
         {'addr':'242868','result':'r6l','regs':{'er6':'%04X'%which}},
         {'symbol':'_module_run_fetch','result':'r0l',
          'regs':{'er0':'%04X'%which}},
         extra=e, drop=((0x0011A61E, 0x0011A640), (0x0011F580, 0x0011F5A0),
                        (0x00104C90, 0x00104E90), (0x00114D60, 0x00114DA0)),
         pin=keep, steps=200000000)

# ------------------------------------------- the hoop's marks and the design
def mod3(name, orig, rebuilt, extra=None, steps=None, pin=None):
    e = collections.OrderedDict()
    e['11F2E4:20'] = '00'
    e.update(CORNERS)
    e.update(w16(0x0011F2F6, 0x0040))
    e.update(w16(0x0011F2F8, 0x0044))
    e.update(w16(0x0011F2FA, 0x0048))
    e.update(w16(0x0011F2FC, 0x004C))
    e.update(w16(0x0011F2FE, 0x0050))
    e.update(w16(0x0011F300, 0x0054))
    e.update(w16(0x0011A626, 0x03E8))
    e.update(w16(0x0011A628, 0x04B0))
    e.update(b8(0x00114D4E, 0x01))
    e.update(b8(0x0011A177, 0x01))
    e.update(b8(0x00114DB0, 0x00))
    e.update(b8(0x0011F59F, 0x00))
    e.update(b8(0x001040BB, 0x01))
    e.update(b8(0x00114D8E, 0x00))
    # The whole of the slot's H'12-byte block, not just its first byte: the
    # count label reads byte three and the labels beside it read the rest, and
    # anything left unpinned is whatever each image's boot happened to leave.
    for _k in range(0x12):
        e.update(b8(0x0011A41A + 0x12*MOD_SLOT + _k, (0x21 + _k) & 0xFF))
    e.update(b8(0x0011A41A + 0x12*MOD_SLOT, MOD_DESIGN))
    e.update(w16(0x00104CCE + 2*MOD_DESIGN, 0x01F4))
    e.update(w16(0x00104D06 + 2*MOD_DESIGN, 0x0258))
    e.update(slotbytes(0x64, 0x64, 0x00, 0x24))
    e.update(w16(0x0011A266 + 0x10*MOD_SLOT, 0x0000))
    e.update(w16(0x0011A268 + 0x10*MOD_SLOT, 0x0000))
    e.update(w16(0x0011F4E0, 0x0000))
    e.update(w16(0x0011F4E2, 0x0000))
    if extra: e.update(extra)
    furn(name, orig, rebuilt, extra=e, steps=steps, pin=pin)

# H'24A62E
for w, h, kind, nm in ((0x03E8, 0x04B0, 0x01, 'the plain cross'),
                       (0x03E8, 0x04B0, 0x00, 'no hoop'),
                       (0x03E8, 0x04B0, 0xAC, 'the quarter lines'),
                       (0x03E8, 0x04B0, 0x07, 'the circle'),
                       (0x0640, 0x0708, 0x03, 'the circle, a big hoop'),
                       (0x0064, 0x0064, 0x01, 'a small hoop'),
                       (0x0000, 0x0000, 0x01, 'nothing at all'),
                       # A height that puts the last dash of the upward run
                       # exactly one pixel above the top, which is where the
                       # loop's bound turns.
                       (0x03E8, 0x04BE, 0x01, 'the last dash on the edge')):
    mod3('module_hoop_marks (%s)'%nm, {'addr':'24A62E'},
         {'symbol':'_module_hoop_marks'},
         extra=dict(list(w16(0x0011A626, w).items()) +
                    list(w16(0x0011A628, h).items()) +
                    list(b8(0x00114D4E, kind).items())),
         steps=400000000)

# H'246EEC
def outline_case(nm, ex, steps=400000000):
    mod3('module_design_outline (%s)'%nm, {'addr':'246EEC'},
         {'symbol':'_module_design_outline'}, extra=ex, steps=steps)

outline_case('the screen just changed', None)
outline_case('waiting, the link busy',
             dict(list(b8(0x0011A177, 0x00).items()) +
                  list(b8(0x0011F29E, 0x01).items())))
outline_case('counting the long wait',
             dict(list(b8(0x0011A177, 0x00).items()) +
                  list(b8(0x00114DB0, 0x01).items()) +
                  list(b8(0x0011F59F, 0x10).items())))
outline_case('the long wait at its end',
             dict(list(b8(0x0011A177, 0x00).items()) +
                  list(b8(0x00114DB0, 0x01).items()) +
                  list(b8(0x0011F59F, 0x46).items())))
outline_case('the long wait over',
             dict(list(b8(0x0011A177, 0x00).items()) +
                  list(b8(0x00114DB0, 0x01).items()) +
                  list(b8(0x0011F59F, 0x47).items())))
outline_case('counting the short wait',
             dict(list(b8(0x0011A177, 0x00).items()) +
                  list(b8(0x0011F59F, 0x02).items())))
outline_case('the short wait over',
             dict(list(b8(0x0011A177, 0x00).items()) +
                  list(b8(0x0011F59F, 0x03).items())))
outline_case('the outline taken away',
             dict(list(b8(0x0011A177, 0x00).items()) +
                  list(b8(0x0011F59F, 0x03).items()) +
                  list(b8(0x00114D8E, 0x0B).items())))
outline_case('nothing drawn yet', w16(0x0011F2E4, 0x0000))
# Both of the bits H'114D50 carries up, so that putting one down is
# something rather than nothing. Safe only because the screen has just
# changed, which is the path that does not ask whether the link is quiet.
outline_case('the flag put down', b8(0x00114D50, 0x30))
for a, nm in ((0x24, 'square on'), (0x00, 'turned right round'),
              (0x48, 'the other way'), (0x30, 'turned a little'),
              (0x12, 'turned back a little'), (0xFF, 'the last angle')):
    outline_case('%s'%nm, b8(0x0011A25A + 0x10*MOD_SLOT + 0x06, a))
outline_case('the arrow mirrored',
             b8(0x0011A25A + 0x10*MOD_SLOT + 0x05, 0x01))
outline_case('moved off centre',
             dict(list(w16(0x0011A266 + 0x10*MOD_SLOT, 0x0064).items()) +
                  list(w16(0x0011A268 + 0x10*MOD_SLOT, 0xFFCE).items())))
outline_case('scaled down',
             dict(list(b8(0x0011A25A + 0x10*MOD_SLOT, 0x32).items()) +
                  list(b8(0x0011A25B + 0x10*MOD_SLOT, 0x28).items())))
outline_case('a design of no size',
             dict(list(w16(0x00104CCE + 2*MOD_DESIGN, 0x0000).items()) +
                  list(w16(0x00104D06 + 2*MOD_DESIGN, 0x0000).items())))
outline_case('the labels left alone', b8(0x00114D8E, 0x05))
outline_case('the size put up with it', b8(0x00114D8E, 0x08))
outline_case('the size put up, the other state', b8(0x00114D8E, 0x09))
outline_case('one state past the two', b8(0x00114D8E, 0x0A))
for kind, nm in ((0xAC, 'the hoop with the quarter lines'),
                 (0x07, 'the hoop with the circle')):
    outline_case(nm, b8(0x00114D4E, kind))

# H'24073E. The same work without any of the drawing.
def corners_case(nm, ex):
    mod3('module_design_corners (%s)'%nm, {'addr':'24073E'},
         {'symbol':'_module_design_corners'}, extra=ex, steps=400000000)

corners_case('a plain one', None)
for a, nm in ((0x24, 'square on'), (0x00, 'turned right round'),
              (0x48, 'the other way'), (0x30, 'turned a little'),
              (0x12, 'turned back a little'), (0xFF, 'the last angle')):
    corners_case(nm, b8(0x0011A25A + 0x10*MOD_SLOT + 0x06, a))
corners_case('the arrow mirrored',
             b8(0x0011A25A + 0x10*MOD_SLOT + 0x05, 0x01))
corners_case('moved off centre',
             dict(list(w16(0x0011A266 + 0x10*MOD_SLOT, 0x0064).items()) +
                  list(w16(0x0011A268 + 0x10*MOD_SLOT, 0xFFCE).items())))
corners_case('scaled down',
             dict(list(b8(0x0011A25A + 0x10*MOD_SLOT, 0x32).items()) +
                  list(b8(0x0011A25B + 0x10*MOD_SLOT, 0x28).items())))
corners_case('a design of no size',
             dict(list(w16(0x00104CCE + 2*MOD_DESIGN, 0x0000).items()) +
                  list(w16(0x00104D06 + 2*MOD_DESIGN, 0x0000).items())))
corners_case('a design one across',
             dict(list(w16(0x00104CCE + 2*MOD_DESIGN, 0x000D).items()) +
                  list(w16(0x00104D06 + 2*MOD_DESIGN, 0x000D).items())))
corners_case('a design that fills the hoop',
             dict(list(w16(0x00104CCE + 2*MOD_DESIGN, 0x0BB8).items()) +
                  list(w16(0x00104D06 + 2*MOD_DESIGN, 0x0DAC).items())))

# ------------------------------------------- the stitch stream
# A block is fifteen bytes of header and then its data: kind, the bytes to a
# row, the rows, two spare, and four words -- the two the drawing uses and two
# it does not. Kind one is a picture of count*size bytes; kind two is eight
# bytes of hoop corners; anything else stops the walk.
REC_AT = 0x000E6A80

def stream_blocks():
    blocks = []
    blocks.append((0x01, 0x03, 0x08, 0x0064, 0x0032,
                   [((k * 0x5B) ^ (k << 3)) & 0xFF for k in range(3*8)]))
    blocks.append((0x02, 0x00, 0x00, 0x0000, 0x0000,
                   [0x20, 0x30, 0x80, 0x40, 0x90, 0x60, 0x28, 0x70]))
    blocks.append((0x01, 0x02, 0x06, 0xFFB0, 0xFFEC,
                   [((k * 0x97) ^ 0x3C) & 0xFF for k in range(2*6)]))
    blocks.append((0x01, 0x04, 0x05, 0x0000, 0x0000,
                   [((k * 0x35) + 0x11) & 0xFF for k in range(4*5)]))
    return blocks

def stream_fill():
    d = collections.OrderedDict()
    d['10C27A:400'] = '00'
    at = 0x0010C27A
    starts = []
    for kind, count, size, x, y, data in stream_blocks():
        starts.append(at)
        hdr = [kind, count, size, 0x11, 0x22,
               (x >> 8) & 0xFF, x & 0xFF, (y >> 8) & 0xFF, y & 0xFF,
               0x00, 0x0A, 0x00, 0x0B, 0x00, 0x0C]
        for i, v in enumerate(hdr):
            d.update(b8(at + i, v))
        at += 15
        for i, v in enumerate(data):
            d.update(b8(at + i, v))
        at += len(data)
    d.update(b8(at, 0x7F))          # a kind nothing claims
    d.update(b8(0x0011A640, 0x04))
    d.update(b8(0x0011A641, 0x00))
    return d, starts

STREAM, STREAM_AT = stream_fill()

def rec_for(n):
    """A record as H'241228 would have filled it, for the n'th block."""
    kind, count, size, x, y, data = stream_blocks()[n]
    d = collections.OrderedDict()
    d['0E6A80:20'] = '00'
    d.update(b8(REC_AT + 0x00, kind))
    d.update(b8(REC_AT + 0x01, count))
    d.update(b8(REC_AT + 0x02, size))
    d.update(w16(REC_AT + 0x06, x))
    d.update(w16(REC_AT + 0x08, y))
    d.update(w32(REC_AT + 0x10, STREAM_AT[n]))
    d.update(b8(REC_AT + 0x14, 0x0F))
    return d

def mod4(name, orig, rebuilt, extra=None, steps=400000000):
    e = collections.OrderedDict(STREAM)
    if extra: e.update(extra)
    mod3(name, orig, rebuilt, extra=e, steps=steps)

# H'2404F0
for n, colour, mode, nm in ((0, 0x02, 0x02, 'a picture into the back'),
                            (0, 0x02, 0x01, 'a picture into the front'),
                            (0, 0x03, 0x02, 'a picture in another colour'),
                            (0, 0x02, 0x03, 'a buffer that is neither'),
                            (1, 0x02, 0x02, 'the corners, into the back'),
                            (1, 0x02, 0x01, 'the corners, into the front'),
                            (2, 0x02, 0x02, 'a picture placed back and up'),
                            (3, 0x02, 0x02, 'a wider picture')):
    mod4('module_block_draw (%s)'%nm,
         {'addr':'2404F0','regs':{'er6':'%X'%REC_AT},
          'stack':{'4':'2:%04X'%colour, '6':'2:%04X'%mode}},
         {'symbol':'_module_block_draw',
          'regs':{'er0':'%X'%REC_AT,'er1':'%02X'%colour,'er2':'%02X'%mode}},
         extra=rec_for(n))
mod4('module_block_draw (a kind nothing claims)',
     {'addr':'2404F0','regs':{'er6':'%X'%REC_AT},
      'stack':{'4':'2:0002', '6':'2:0002'}},
     {'symbol':'_module_block_draw',
      'regs':{'er0':'%X'%REC_AT,'er1':'02','er2':'02'}},
     extra=dict(list(rec_for(0).items()) + list(b8(REC_AT, 0x05).items())))

# H'2403D2
for which, colour, mode, skip, nm in (
        (0xFF, 0x02, 0x02, 0x03, 'the whole stream, one left out'),
        (0xFF, 0x02, 0x01, 0x03, 'the whole stream, into the front'),
        (0xFF, 0x02, 0x02, 0x00, 'the whole stream, nothing left out'),
        (0xFF, 0x02, 0x03, 0x03, 'the whole stream, neither buffer'),
        (0x01, 0x02, 0x02, 0x00, 'the first block on its own'),
        (0x02, 0x02, 0x02, 0x00, 'the second on its own'),
        (0x04, 0x02, 0x02, 0x00, 'the last on its own'),
        # The first block left out: the only skip that tells a walk starting
        # at one from a walk starting at nought, because index nought and
        # index one both give the first block.
        (0xFF, 0x02, 0x02, 0x01, 'the whole stream, the first left out')):
    mod4('module_stitches_walk (%s)'%nm,
         {'addr':'2403D2','regs':{'er6':'%02X'%which},
          'stack':{'4':'2:%04X'%colour,'6':'2:%04X'%mode,'8':'2:%04X'%skip}},
         {'symbol':'_module_stitches_walk',
          'regs':{'er0':'%02X'%which,'er1':'%02X'%colour,'er2':'%02X'%mode},
          'stack':{'4':'4:%08X'%skip}})
mod4('module_stitches_walk (a block the stream cannot give up)',
     {'addr':'2403D2','regs':{'er6':'FF'},
      'stack':{'4':'2:0002','6':'2:0002','8':'2:0000'}},
     {'symbol':'_module_stitches_walk',
      'regs':{'er0':'FF','er1':'02','er2':'02'},'stack':{'4':'4:00000000'}},
     extra=b8(0x0011A640, 0x08))

# H'2403A2
mod4('module_stitches_draw', {'addr':'2403A2'},
     {'symbol':'_module_stitches_draw'})
mod4('module_stitches_draw (nothing to draw)', {'addr':'2403A2'},
     {'symbol':'_module_stitches_draw'}, extra=b8(0x0011A640, 0x00))

# ------------------------------------------- the tick, and the pedal under it
# H'208698 and H'209AF0 have been written since part 12 but never had cases of
# their own: they are five calls and three calls, and everything under them is
# covered. The fill is the six callees' own fills put together -- every wide
# key first, in the order they had, and then every narrow one, so that a wipe
# belonging to one cannot cover a pin belonging to another.
def merged_fill(names):
    wide, narrow = collections.OrderedDict(), collections.OrderedDict()
    for nm in names:
        f = [c for c in cases if c['name'] == nm][0]['fill']
        for k, v in f.items():
            (narrow if int(k.split(':')[1], 16) == 1 else wide)[k] = v
    out = collections.OrderedDict(wide)
    out.update(narrow)
    return out

TICK_PARTS = ['analog_scan', 'stitch_state_init (no queue)',
              'pedal_scan (zone 2)', 'pedal_hold_service (release)',
              'speed_service', 'main_motor_service (running)']
TICK_SEED = {'114DD2': '00', '114DD3': '50', '114DD4': '00', '114DD5': '00'}
ANALOG = {'0': '11', '1': '22', '2': '33', '3': '44',
          '4': '55', '5': '66', '6': '77', '7': '88'}

def tick_case(name, addr, sym, parts, extra=None):
    f = merged_fill(parts)
    if extra: f.update(extra)
    c = collections.OrderedDict()
    c['name'] = name
    c['boot'] = 1900000
    c['seed'] = TICK_SEED
    # The seven counters H'20AAE0 bumps from the millisecond interrupt, and
    # the timer register the motor cases already leave out. A routine this
    # long takes a different number of steps on the two sides and so a
    # different number of ticks, and none of these is comparable because of
    # it. Everything else these routines touch still is.
    c['exclude'] = ['FFFF95:3', '11A6D8:2', '114DDA:0C', '11A802:1']
    c['analog'] = ANALOG
    c['fill'] = f
    c['original'] = {'addr': addr}
    c['rebuilt'] = {'symbol': sym}
    new.append(c)

# Bisect: each of the three run on its own against the merged fill.
tick_case('pedal_scan (merged fill)', '20946E', '_pedal_scan', TICK_PARTS[2:5])
tick_case('pedal_hold_service (merged fill)', '2099FA', '_pedal_hold_service',
          TICK_PARTS[2:5])
tick_case('speed_service (merged fill)', '2099D0', '_speed_service',
          TICK_PARTS[2:5])
tick_case('pedal_service (the pedal live)', '209AF0', '_pedal_service',
          TICK_PARTS[2:5])
tick_case('pedal_service (the pedal locked out)', '209AF0', '_pedal_service',
          TICK_PARTS[2:5], extra=b8(0x00FFFEF7, 0x80))
tick_case('analog_scan (merged fill)', '209E48', '_analog_scan', TICK_PARTS)
tick_case('stitch_state_init (merged fill)', '208210', '_stitch_state_init',
          TICK_PARTS)
tick_case('main_motor_service (merged fill)', '20DB24', '_main_motor_service',
          TICK_PARTS)
tick_case('service_tick', '208698', '_service_tick', TICK_PARTS)
tick_case('service_tick (the pedal locked out)', '208698', '_service_tick',
          TICK_PARTS, extra=b8(0x00FFFEF7, 0x80))

# H'23180C
furn('module_go_sewing', {'addr':'23180C'}, {'symbol':'_module_go_sewing'})

# ------------------------------------------- what a module screen does on a pass
# The colour records live at H'104D4A -- the same RAM the picking grid's
# thumbnails use -- so the two cannot both be in a fill. States four and five
# read the records, and a record the fill does not pin sends the walk off into
# memory the two images do not agree about. The records win here; the grid
# case draws different pixels for it and does not care.
_cb = [c for c in cases if c['name'] == 'module_colour_bitmap index=00 first'][0]
COLOUR_RECORDS = collections.OrderedDict(
    (k, v) for k, v in _cb['fill'].items()
    if not k.startswith('040000') and not k.startswith('0F1610')
    and k != '104D4A:400')

def tick2(nm, ex, steps=400000000):
    e = collections.OrderedDict(STREAM)
    e.update(COLOUR_RECORDS)
    e.update(b8(0x00114DB9, 0x00))
    e.update(b8(0x0011F304, 0x01))
    e.update(b8(0x0011F305, 0x01))
    e.update(b8(0x0011F30E, 0x01))
    e.update(b8(0x0011F5A0, 0x00))
    e.update(b8(0x0011A177, 0x00))
    e.update(b8(0x0011B0A8, 0x00))
    e.update(b8(0x00114D86, 0x00))
    e.update(b8(0x00114D88, 0x01))
    e.update(b8(0x00114D89, 0x02))
    e.update(b8(0x00114D96, 0x01))
    # Everything the four labels read: how many colours there are, whether
    # they are wanted at all, and the two bytes the percentage is worked out
    # from. Left unpinned, each image's boot decides them separately.
    e.update(b8(0x00114D8D, 0x06))
    e.update(b8(0x00114D91, 0x01))
    e.update(b8(0x00114DBC, 0x20))
    e.update(b8(0x00114DBE, 0x03))
    e.update(b8(0x00114DBF, 0x04))
    e.update(b8(0x00114D62, 0x00))
    e.update(b8(0x00114D66, 0x00))
    e.update(b8(0x00114D7E, 0x00))
    e.update(b8(0x00114DAD, 0x00))
    e.update(b8(0x00114D98, 0x00))
    e.update(w16(0x0011F4E4, 0x0064))
    e.update(b8(0x001040B0, 0x03))
    e.update(b8(0x001040B9, 0x02))
    e.update(b8(0x00114D50, 0x00))
    e.update(b8(0x00114D53, 0x50))    # a hoop that can be sewn
    e.update(b8(0x001040B6, 0x00))
    e.update(w16(0x0011F4E0, 0x0064))  # the two sizes told apart
    e.update(w16(0x0011F4E2, 0x0032))
    if ex: e.update(ex)
    mod3('module_screen_tick (%s)'%nm, {'addr':'2480D6'},
         {'symbol':'_module_screen_tick'}, extra=e, steps=steps)

tick2('the module reporting', b8(0x00114DB9, 0x01))
tick2('nothing asked for', dict(list(b8(0x0011F304, 0x00).items()) +
                                list(b8(0x0011A177, 0x00).items())))
tick2('the screen just changed', dict(list(b8(0x0011F304, 0x00).items()) +
                                      list(b8(0x0011A177, 0x01).items()) +
                                      list(b8(0x0011F5A0, 0x10).items())))
tick2('counting down', b8(0x0011F5A0, 0x10))
for st in (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
           0x0A, 0x0B, 0x0C):
    tick2('state %02X'%st, b8(0x00114D8E, st))
tick2('a message still going out', dict(list(b8(0x00114D8E, 0x04).items()) +
                                        list(b8(0x00114D86, 0x01).items())))
tick2('the third state with nothing to say',
      dict(list(b8(0x00114D8E, 0x03).items()) +
           list(b8(0x00114D88, 0x00).items())))
tick2('the grid with nothing to draw',
      dict(list(b8(0x00114D8E, 0x06).items()) +
           list(b8(0x00114D88, 0x00).items())))
tick2('the colours with no outline',
      dict(list(b8(0x00114D8E, 0x07).items()) +
           list(b8(0x00114D88, 0x00).items())))
tick2('the colours with the screen put back',
      dict(list(b8(0x00114D8E, 0x07).items()) +
           list(b8(0x0011F304, 0x01).items()) +
           list(b8(0x0011F4E6, 0x01).items())))
tick2('the colours with no colour on',
      dict(list(b8(0x00114D8E, 0x07).items()) +
           list(b8(0x001040B0, 0x00).items()) +
           list(b8(0x001040B9, 0x00).items())))
tick2('the colours with the hoop measured',
      dict(list(b8(0x00114D8E, 0x07).items()) +
           list(b8(0x00114D51, 0x02).items()) +
           list(b8(0x001040B1, 0x01).items())))
# The box table put back at the corner for one case: everything the colour
# strip paints is inside the rectangle the stitch drawing blacks afterwards,
# so a box painted one way or the other cannot be seen at the usual origin.
tick2('the colours with the boxes clear of the clear',
      dict(list(b8(0x00114D8E, 0x07).items()) +
           list(w16(0x0011B0B4, 0x0000).items())))
tick2('the size with the picture off',
      dict(list(b8(0x00114D8E, 0x08).items()) +
           list(b8(0x00114D88, 0x00).items())))

# ------------------------------------------- the twelve steps that start sewing
# Step nought sends message H'0B and then waits for it, which only the link's
# own interrupt can end, so it is the one step no case can run. Every other
# step sends and returns without waiting.
def sew_case(nm, ex, which=0x01, steps=100000000):
    e = collections.OrderedDict(STREAM)
    e.update(b8(0x001040B8, 0x02))
    e.update(b8(0x001040B9, 0x00))
    e.update(b8(0x001040BA, 0x00))
    e.update(b8(0x001040B1, 0x00))
    e.update(b8(0x001040B2, 0x00))
    e.update(b8(0x001040B5, 0x00))
    e.update(b8(0x001040BB, 0x00))
    e.update(b8(0x0011F568, 0x01))
    e.update(b8(0x0011F2A2, 0x00))
    e.update(b8(0x0011A61A, 0x00))
    e.update(b8(0x0011A61B, 0x00))
    e.update(b8(0x00114D65, 0x00))
    e.update(b8(0x00114D66, 0x00))
    e.update(b8(0x00114D72, 0x00))
    e.update(b8(0x00114D73, 0x00))
    e.update(b8(0x00114D92, 0x00))
    e.update(b8(0x00114D93, 0x00))
    e.update(b8(0x00114D9F, 0x00))
    e.update(b8(0x00114DBC, 0x00))
    e.update(b8(0x00114DBD, 0x00))
    e.update(b8(0x00114D8D, 0x00))
    e.update(b8(0x00114D51, 0x00))
    e.update(b8(0x0011A63C, 0x00))
    e.update(w16(0x0011F292, 0xAAAA))
    e.update(b8(0x000FFE80, 0x05))
    e.update(b8(0x00100255, 0x02))
    for _k in range(0x0C):
        e.update(b8(0x0011A61E + _k, (0xA7 + _k) & 0xFF))
    if ex: e.update(ex)
    mod3('module_sew_step (%s)'%nm,
         {'addr':'23C6FE','result':'r6l','regs':{'er6':'%02X'%which}},
         {'symbol':'_module_sew_step','result':'r0l',
          'regs':{'er0':'%02X'%which}}, extra=e, steps=steps)

for st in range(1, 0x0C):
    sew_case('step %02X'%st, b8(0x001040B8, st))
sew_case('a step past the last', b8(0x001040B8, 0x0C))
sew_case('a step past the last, the link busy',
         dict(list(b8(0x001040B8, 0x0C).items()) +
              list(b8(0x0011F29E, 0x01).items())))
sew_case('the link busy', dict(list(b8(0x001040B8, 0x04).items()) +
                               list(b8(0x0011F29E, 0x01).items())))
sew_case('the first step, the hoop measured',
         dict(list(b8(0x001040B8, 0x01).items()) +
              list(b8(0x00114D51, 0x02).items()) +
              list(b8(0x001040B1, 0x01).items())))
sew_case('the first step, the hoop measured the other way',
         dict(list(b8(0x001040B8, 0x01).items()) +
              list(b8(0x00114D51, 0x02).items()) +
              list(b8(0x001040B1, 0x04).items())))
sew_case('the first step, the other screen',
         dict(list(b8(0x001040B8, 0x01).items()) +
              list(b8(0x0011F568, 0x02).items())))
sew_case('the sixth step with a colour asked for',
         dict(list(b8(0x001040B8, 0x06).items()) +
              list(b8(0x001040B9, 0x03).items())))
sew_case('the sixth step, a colour and an offset',
         dict(list(b8(0x001040B8, 0x06).items()) +
              list(b8(0x001040B9, 0x03).items()) +
              list(b8(0x0011A41D + 0x12*MOD_SLOT, 0x00).items()) +
              list(b8(0x00114DBC, 0x1E).items())))
sew_case('the sixth step, a design past the page',
         dict(list(b8(0x001040B8, 0x06).items()) +
              list(b8(0x000FFE80, 0x40).items())))
sew_case('the last step, the other screen',
         dict(list(b8(0x001040B8, 0x0B).items()) +
              list(b8(0x0011F568, 0x02).items())))
sew_case('the last step with a colour asked for',
         dict(list(b8(0x001040B8, 0x0B).items()) +
              list(b8(0x001040B9, 0x03).items())))


# ------------------------------------------ the seven steps of one colour's run
# H'23D150 is one call for the whole walk: every step ends by servicing the
# host and, while the step number is still under seven, goes round again. So
# the only ways a case can reach the end are the turns back near the top, the
# two steps that give up on their own, and the steps that walk through to
# seven without sending -- once a message has gone out the wait that follows
# it can only be ended by the link's own interrupt.
#
# Steps three to six carry a counter in E3. Nothing reaches them without
# going through step two, which puts it to nought, so a case that starts at
# one of them says so by handing the original a nought in ER3.
def run_case(nm, ex, steps=400000000, er3=False):
    e = collections.OrderedDict()
    e.update(b8(0x00114D51, 0x01))
    e.update(b8(0x00114D53, 0x00))
    e.update(b8(0x00114D55, 0x00))
    e.update(b8(0x00114D56, 0x00))
    e.update(b8(0x0011A41A, 0x01))
    e.update(b8(0x001040AE, 0x02))
    e.update(b8(0x001040AF, 0x05))
    e.update(b8(0x001040B0, 0x00))
    e.update(b8(0x001040B1, 0x01))
    e.update(b8(0x001040B2, 0x00))
    e.update(b8(0x001040B3, 0x07))
    e.update(b8(0x001040B5, 0x00))
    e.update(b8(0x001040B6, 0x00))
    e.update(b8(0x001040B9, 0x00))
    e.update(b8(0x0011F534, 0x00))
    e.update(b8(0x0011F56F, 0x00))
    e.update(b8(0x0011F571, 0x00))
    e.update(b8(0x0011F29E, 0x00))
    e.update(b8(0x0011F2B6, 0x00))
    e.update(b8(0x0011F2A1, 0x00))
    e.update(b8(0x00114D50, 0x00))
    e.update(b8(0x0011A619, 0x00))
    e.update(b8(0x0011A61B, 0x00))
    e.update(b8(0x0011A63C, 0x00))
    e.update(w16(0x00114D4C, 0x0000))
    if ex: e.update(ex)
    orig = {'addr': '23D150'}
    if er3: orig['regs'] = {'er3': '00000000'}
    mod3("module_colour_run (%s)" % nm, orig,
         {'symbol': '_module_colour_run'}, extra=e, steps=steps, pin=e)

# The turns back at the top.
run_case('neither bit of H\'114D51', b8(0x00114D51, 0x00))
run_case('neither bit, the other count',
         dict(list(b8(0x00114D51, 0x00).items()) +
              list(b8(0x001040AE, 0x0B).items())))
run_case('the hoop measured, the first way', b8(0x00114D51, 0x02))
run_case('the hoop measured, the second way',
         dict(list(b8(0x00114D51, 0x02).items()) +
              list(b8(0x001040B1, 0x04).items())))
run_case('the link asked for by another',
         dict(list(b8(0x00114D55, 0x04).items()) +
              list(b8(0x001040B1, 0x04).items())))
run_case('nothing in the slot', b8(0x0011A41A, 0x00))

# The step past the last, which is the one ordinary way out.
run_case('a step past the last', None)
run_case('a step past the last, the plain design',
         b8(0x001040B2, 0x01))
run_case('a step past the last, the plain design after all',
         dict(list(b8(0x001040B2, 0x01).items()) +
              list(b8(0x0011A25A + 0x10*0x02, 0x32).items()) +
              list(b8(0x0011A25B + 0x10*0x02, 0x32).items()) +
              list(b8(0x0011A25F + 0x10*0x02, 0x00).items()) +
              list(b8(0x0011A260 + 0x10*0x02, 0x24).items()) +
              list(b8(0x0011A660, 0x02).items())))
run_case('a step well past the last', b8(0x001040B3, 0x20))
run_case('a step past the last, the other count',
         dict(list(b8(0x001040B3, 0x0A).items()) +
              list(b8(0x001040B2, 0x02).items())))

# Step one, which gives up three different ways.
run_case('step one, the hoop taken off',
         dict(list(b8(0x001040B3, 0x01).items()) +
              list(w16(0x00114D4C, 0x4000).items())))
run_case('step one, the whole of it sewn',
         dict(list(b8(0x001040B3, 0x01).items()) +
              list(b8(0x00114D56, 0x64).items())))
run_case('step one, the whole of it sewn, the other hoop',
         dict(list(b8(0x001040B3, 0x01).items()) +
              list(b8(0x00114D56, 0x64).items()) +
              list(b8(0x001040B1, 0x04).items())))
run_case('step one, the last colour reached',
         dict(list(b8(0x001040B3, 0x01).items()) +
              list(b8(0x001040AE, 0x0F).items())))
run_case('step one, past the last colour, the other hoop',
         dict(list(b8(0x001040B3, 0x01).items()) +
              list(b8(0x00114D51, 0x02).items()) +
              list(b8(0x001040B1, 0x04).items()) +
              list(b8(0x001040AF, 0x11).items())))

# Steps four, five and six, which walk through to seven without sending as
# long as the hoop is not one that can be sewn.
run_case('step six', b8(0x001040B3, 0x06), er3=True)
run_case('step six, the hoop measured the first way',
         dict(list(b8(0x001040B3, 0x06).items()) +
              list(b8(0x00114D51, 0x02).items())), er3=True)
run_case('step six, the hoop measured the second way',
         dict(list(b8(0x001040B3, 0x06).items()) +
              list(b8(0x00114D51, 0x02).items()) +
              list(b8(0x001040B1, 0x04).items())), er3=True)
run_case('step six, some way through the colour',
         dict(list(b8(0x001040B3, 0x06).items()) +
              list(b8(0x001040AE, 0x09).items())), er3=True)
run_case('step six, the bar already further on',
         dict(list(b8(0x001040B3, 0x06).items()) +
              list(b8(0x00114D56, 0x50).items())), er3=True)
run_case('step six, a colour asked for',
         dict(list(b8(0x001040B3, 0x06).items()) +
              list(b8(0x001040B9, 0x03).items()) +
              list(b8(0x001040AE, 0x04).items())), er3=True)
run_case('step six, the hoop one that can be sewn',
         dict(list(b8(0x001040B3, 0x06).items()) +
              list(b8(0x00114D53, 0x50).items())), er3=True)
run_case('step five', b8(0x001040B3, 0x05), er3=True)
run_case('step five, the colour walk on its last step',
         dict(list(b8(0x001040B3, 0x05).items()) +
              list(b8(0x001040B6, 0x03).items())), er3=True)
run_case('step four, the hoop not one that can be sewn',
         b8(0x001040B3, 0x04), er3=True)
run_case('step four, the report bit up',
         dict(list(b8(0x001040B3, 0x04).items()) +
              list(b8(0x00114D51, 0x41).items())), er3=True)


# --------------------------------------------- the colour taken back again
# H'23D66A holds two walks and H'114DAB picks between them. Both are one
# call for the whole walk, so the ways a case can reach the end are the same
# as H'23D150's: the turns back near the top, and the runs that reach the
# last step without sending. The step that unwinds the row of boxes is not
# among them -- the step after it sends and the step after that waits.
def back_case(nm, ex, steps=400000000, er3=False):
    e = collections.OrderedDict()
    e.update(b8(0x00114D51, 0x01))
    e.update(b8(0x00114D53, 0x00))
    e.update(b8(0x00114D56, 0x00))
    e.update(b8(0x00114D84, 0x01))
    e.update(b8(0x00114D88, 0x00))
    e.update(b8(0x00114DAB, 0x01))
    e.update(b8(0x001040AE, 0x02))
    e.update(b8(0x001040AF, 0x05))
    e.update(b8(0x001040B0, 0x01))
    e.update(b8(0x001040B1, 0x01))
    e.update(b8(0x001040B2, 0x00))
    e.update(b8(0x001040B3, 0x08))
    e.update(b8(0x001040B5, 0x00))
    e.update(b8(0x001040B6, 0x00))
    e.update(b8(0x001040B9, 0x00))
    e.update(b8(0x0011A177, 0x01))
    e.update(b8(0x0011F304, 0x00))
    e.update(b8(0x0011F532, 0x00))
    e.update(b8(0x0011F534, 0x00))
    e.update(b8(0x0011F570, 0x00))
    e.update(b8(0x0011F571, 0x00))
    e.update(b8(0x0011F29E, 0x00))
    e.update(b8(0x0011F2B6, 0x00))
    e.update(b8(0x0011F2A1, 0x00))
    e.update(b8(0x0011F2A2, 0x00))
    e.update(b8(0x00114D50, 0x00))
    e.update(b8(0x0011A619, 0x00))
    e.update(b8(0x0011A61B, 0x00))
    e.update(w16(0x00114D4C, 0x0000))
    if ex: e.update(ex)
    orig = {'addr': '23D66A'}
    if er3: orig['regs'] = {'er3': '00000000'}
    mod3("module_colour_back (%s)" % nm, orig,
         {'symbol': '_module_colour_back'}, extra=e, steps=steps, pin=e)

# The turn back at the very top, which both walks share.
back_case('neither bit of H\'114D51', b8(0x00114D51, 0x00))
back_case('neither bit, a count to put back',
         dict(list(b8(0x00114D51, 0x00).items()) +
              list(b8(0x001040AE, 0x07).items())))
back_case('the hoop measured, the first way', b8(0x00114D51, 0x02))
back_case('the hoop measured, the second way',
         dict(list(b8(0x00114D51, 0x02).items()) +
              list(b8(0x001040B1, 0x04).items())))

# The long walk: the whole run given up.
back_case('the whole run given up, the last step', None)
back_case('the whole run given up, a step past the last',
         b8(0x001040B3, 0x0E))
back_case('the whole run given up, the bar drawn',
         b8(0x001040B3, 0x07), er3=True)
back_case('the whole run given up, the bar part of the way',
         dict(list(b8(0x001040B3, 0x07).items()) +
              list(b8(0x001040AE, 0x09).items())), er3=True)
back_case('the whole run given up, the bar nearly full',
         dict(list(b8(0x001040B3, 0x07).items()) +
              list(b8(0x001040AE, 0x0B).items())), er3=True)
back_case('the whole run given up, the bar full',
         dict(list(b8(0x001040B3, 0x07).items()) +
              list(b8(0x001040AE, 0x0F).items())), er3=True)
back_case('the whole run given up, the bar past full',
         dict(list(b8(0x001040B3, 0x07).items()) +
              list(b8(0x001040AE, 0x10).items())), er3=True)
back_case('the whole run given up, the bar and the other count',
         dict(list(b8(0x001040B3, 0x07).items()) +
              list(b8(0x00114D51, 0x02).items()) +
              list(b8(0x001040B1, 0x04).items()) +
              list(b8(0x001040AF, 0x0D).items())), er3=True)
back_case('the whole run given up, a stop asked for',
         dict(list(b8(0x001040B3, 0x07).items()) +
              list(b8(0x001040B5, 0x01).items())), er3=True)
back_case('the whole run given up, the boxes put out',
         b8(0x001040B3, 0x06), er3=True)
back_case('the whole run given up, the boxes and the other count',
         dict(list(b8(0x001040B3, 0x06).items()) +
              list(b8(0x00114D51, 0x02).items()) +
              list(b8(0x001040B1, 0x04).items())), er3=True)
back_case('the whole run given up, the boxes with the hoop not measured',
         dict(list(b8(0x001040B3, 0x06).items()) +
              list(b8(0x00114D51, 0x01).items())), er3=True)
back_case('the whole run given up, the boxes with the hoop measured',
         dict(list(b8(0x001040B3, 0x06).items()) +
              list(b8(0x00114D51, 0x02).items()) +
              list(b8(0x001040B1, 0x01).items())), er3=True)
back_case('the whole run given up, a stop asked for on another screen',
         dict(list(b8(0x001040B3, 0x02).items()) +
              list(b8(0x00114D8E, 0x04).items()) +
              list(b8(0x0011F534, 0x01).items())))
back_case('the whole run given up, the hoop taken off',
         dict(list(b8(0x001040B3, 0x01).items()) +
              list(w16(0x00114D4C, 0x4000).items())))
back_case('the whole run given up, nothing to report',
         b8(0x001040B3, 0x02))
back_case('the whole run given up, no screen to go to',
         dict(list(b8(0x001040B3, 0x03).items()) +
              list(b8(0x00114D8E, 0x07).items()) +
              list(b8(0x0011A177, 0x00).items()) +
              list(b8(0x0011F532, 0x00).items())))

# The short walk: only the last colour taken back.
back_case('one colour back, nothing to take back',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040AE, 0x00).items())))
back_case('one colour back, no colour in hand',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B0, 0x00).items())))
back_case('one colour back, the colour past the count',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B0, 0x05).items())))
back_case('one colour back, the last step',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x0A).items())), er3=True)
back_case('one colour back, a step past the last',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x0C).items())), er3=True)
back_case('one colour back, the bar part of the way',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x0A).items()) +
              list(b8(0x001040AE, 0x09).items())), er3=True)
back_case('one colour back, the bar already further on',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x0A).items()) +
              list(b8(0x00114D56, 0x50).items())), er3=True)
back_case('one colour back, a stop asked for',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x0A).items()) +
              list(b8(0x001040B5, 0x01).items())), er3=True)
back_case('one colour back, the colour walk left over',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x09).items())), er3=True)
back_case('one colour back, the colour walk on its last step',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x09).items()) +
              list(b8(0x001040B6, 0x03).items())), er3=True)
back_case('one colour back, the last colour of one',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040AE, 0x01).items()) +
              list(b8(0x001040B0, 0x01).items())), er3=True)
back_case('one colour back, the colour started off',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x08).items())), er3=True)
back_case('one colour back, the hoop taken off',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x01).items()) +
              list(w16(0x00114D4C, 0x4000).items())))
back_case('one colour back, a stop asked for on another screen',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x02).items()) +
              list(b8(0x00114D8E, 0x04).items()) +
              list(b8(0x0011F534, 0x01).items())))
back_case('one colour back, nothing to report',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x02).items())))
back_case('one colour back, no screen to go to',
         dict(list(b8(0x00114DAB, 0x00).items()) +
              list(b8(0x001040B3, 0x03).items()) +
              list(b8(0x00114D8E, 0x07).items()) +
              list(b8(0x0011A177, 0x00).items()) +
              list(b8(0x0011F532, 0x00).items())))


# --------------------------------------------------- screen H'37's own press
# H'114D8E is put to seven before anything else and nothing happens while
# the module says it is busy with that, so every case has to leave H'114D72,
# H'1040B4 and H'114DB9 at nought and the link quiet.
def sewscr(nm, ex, steps=400000000):
    e = collections.OrderedDict()
    e.update(b8(0x00114D51, 0x01))
    e.update(b8(0x00114D53, 0x00))
    e.update(b8(0x00114D56, 0x00))
    e.update(b8(0x00114D62, 0x00))
    e.update(b8(0x00114D65, 0x00))
    e.update(b8(0x00114D66, 0x00))
    e.update(b8(0x00114D72, 0x00))
    e.update(b8(0x00114D84, 0x00))
    e.update(b8(0x00114D88, 0x00))
    e.update(b8(0x00114D89, 0x00))
    e.update(b8(0x00114D8F, 0x00))
    e.update(b8(0x00114D97, 0x00))
    e.update(b8(0x00114D9F, 0x00))
    e.update(b8(0x00114DAB, 0x01))
    e.update(b8(0x00114DB9, 0x00))
    e.update(b8(0x001040AE, 0x02))
    e.update(b8(0x001040AF, 0x05))
    e.update(b8(0x001040B0, 0x01))
    e.update(b8(0x001040B1, 0x01))
    e.update(b8(0x001040B2, 0x00))
    e.update(b8(0x001040B3, 0x08))
    e.update(b8(0x001040B4, 0x00))
    e.update(b8(0x001040B5, 0x00))
    e.update(b8(0x001040B6, 0x00))
    e.update(b8(0x001040B8, 0x0C))
    e.update(b8(0x001040B9, 0x00))
    e.update(b8(0x001040BA, 0x00))
    e.update(b8(0x0011A177, 0x01))
    e.update(b8(0x0011A41A, 0x01))
    e.update(b8(0x0011A63D, 0x00))
    e.update(b8(0x0011F304, 0x00))
    e.update(b8(0x0011F532, 0x00))
    e.update(b8(0x0011F534, 0x00))
    e.update(b8(0x0011F570, 0x00))
    e.update(b8(0x0011F571, 0x00))
    e.update(b8(0x0011F29E, 0x00))
    e.update(b8(0x0011F2B6, 0x00))
    e.update(b8(0x0011F2A1, 0x00))
    e.update(b8(0x0011F2A2, 0x00))
    e.update(b8(0x00114D50, 0x00))
    e.update(b8(0x0011A619, 0x00))
    e.update(b8(0x0011A61B, 0x00))
    e.update(w16(0x00114D4C, 0x0000))
    if ex: e.update(ex)
    mod3("module_sew_screen (%s)" % nm, {'addr': '23078A'},
         {'symbol': '_module_sew_screen'}, extra=e, steps=steps, pin=e)

def sewpress(b, v=None):
    # The module screens' fill moves the box table's own origin down clear
    # of the percentage bar -- H'11B0B4 holds H'50 -- and the hit test adds
    # that origin to every box before it compares, so a press has to be H'50
    # further down the glass than the table says.
    bx = 0x0C + 0x10*((b - 1) % 12)
    by = 0x24 + 0x10*((b - 1) // 12) + 0x50
    return dict(list(b8(0x00FFFED9, bx).items()) +
                list(b8(0x00FFFEDA, by).items()) +
                list(boxval(b, b if v is None else v).items()))

def sewboth(a, b):
    return dict(list(a.items()) + list(b.items()))

sewscr('nothing pressed', None)
sewscr('the module busy',
       sewboth(sewpress(0x02), b8(0x00114D72, 0x01)))
sewscr('the module busy on the link',
       sewboth(sewpress(0x02), b8(0x00114D50, 0x20)))
sewscr('a value past the table', sewpress(0x01, 0x16))
sewscr('a value of nought', sewpress(0x01, 0x00))

# The two keys that take the run away, on the ways they turn back.
for v, nm in ((0x01, 'the stitch screen'), (0x15, 'the plain screen')):
    sewscr('%s, something to report' % nm,
           sewboth(sewpress(v), b8(0x00114D51, 0x41)))
    sewscr('%s, a stop asked for' % nm,
           sewboth(sewpress(v), b8(0x0011F534, 0x01)))
    sewscr('%s, the sewing walk past its last step' % nm,
           sewboth(sewpress(v), b8(0x001040B8, 0x0C)))
    sewscr('%s, the run walk past its last step' % nm,
           sewboth(sewpress(v),
                   dict(list(b8(0x001040B8, 0x0B).items()) +
                        list(b8(0x0011A63D, 0x0D).items()))))

# The colour run and the colour taken back, on the steps that walk out.
sewscr('a colour run', sewboth(sewpress(0x02), b8(0x001040B3, 0x07)))
sewscr('a colour run, nothing in the slot',
       sewboth(sewpress(0x02), b8(0x0011A41A, 0x00)))
sewscr('a colour taken back', sewpress(0x14))
sewscr('a colour taken back, the last colour only',
       sewboth(sewpress(0x14),
               dict(list(b8(0x00114DAB, 0x00).items()) +
                    list(b8(0x001040B3, 0x0A).items()))))

# The two corners of the hoop.
sewscr('the second corner', sewpress(0x03))
sewscr('the second corner, the hoop measured',
       sewboth(sewpress(0x03),
               dict(list(b8(0x00114D51, 0x02).items()) +
                    list(b8(0x001040B1, 0x04).items()))))
sewscr('the first corner', sewpress(0x04))
sewscr('the first corner, the hoop measured',
       sewboth(sewpress(0x04),
               dict(list(b8(0x00114D51, 0x02).items()) +
                    list(b8(0x001040B1, 0x01).items()))))
sewscr('the first corner, the line taken by another',
       sewboth(sewpress(0x04),
               dict(list(b8(0x00114D51, 0x02).items()) +
                    list(b8(0x001040B1, 0x00).items()))))

# The colour boxes along the strip.
for b in (0x05, 0x06, 0x07, 0x0A, 0x13):
    sewscr('the box at %02X' % b, sewpress(b))
sewscr('a box one past the count', sewpress(0x07))
sewscr('a box well past the count',
       sewboth(sewpress(0x13), b8(0x001040AE, 0x01)))
sewscr('a box that is not its own number', sewpress(0x06, 0x0A))
sewscr('a box that is not its own number, further along',
       sewpress(0x03, 0x07))
sewscr('a box with the hoop one that can be sewn',
       sewboth(sewpress(0x06), b8(0x00114D53, 0x50)))
sewscr('a box inside the other count',
       sewboth(sewpress(0x08), b8(0x001040B1, 0x04)))

# H'225AE4
for nm, arrived, relayout, ex in (('just arrived', 1, 0, None),
                                  ('laid out again', 0, 1, None),
                                  ('a plain pass', 0, 0, None),
                                  ('a press on a colour box', 0, 0,
                                   dict(list(b8(0x00FFFED9, 0x4C).items()) +
                                        list(b8(0x00FFFEDA, 0x24).items()) +
                                        list(boxval(6, 6).items())))):
    e = pat_extra(ex)
    dispatch_case(nm, 0x37, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['steps'] = 60000000
    new[-1]['name'] = 'screen_body_37 (%s)' % nm


# --------------------------------------------- screen H'24's own press
# Three boxes and no waiting: every arm sends its message and returns, so
# unlike the rest of this cluster the whole routine is reachable.
def turnscr(nm, ex, steps=400000000):
    e = collections.OrderedDict()
    e.update(b8(0x00FFFEC0, 0x04))
    e.update(b8(0x00FFFEC1, 0x00))
    e.update(b8(0x00114D50, 0x00))
    e.update(b8(0x00114D62, 0x00))
    e.update(b8(0x00114D65, 0x00))
    e.update(b8(0x00114D66, 0x00))
    e.update(b8(0x00114D72, 0x00))
    e.update(b8(0x00114D98, 0x00))
    e.update(b8(0x00114D99, 0x00))
    e.update(b8(0x00114D66, 0x00))
    e.update(b8(0x00114D83, 0x00))
    e.update(b8(0x00FFFEC4, 0x00))
    e.update(b8(0x00114D9A, 0x00))
    e.update(b8(0x00114DB3, 0x00))
    e.update(b8(0x00114DB4, 0x00))
    e.update(b8(0x00114DB9, 0x00))
    e.update(b8(0x0011A618, 0x00))
    e.update(b8(0x0011A61A, 0x00))
    e.update(b8(0x0011F29E, 0x00))
    e.update(b8(0x0011F2A1, 0x00))
    e.update(b8(0x0011F2B6, 0x00))
    if ex: e.update(ex)
    mod3("module_turn_screen (%s)" % nm, {'addr': '230110'},
         {'symbol': '_module_turn_screen'}, extra=e, steps=steps, pin=e)

turnscr('nothing pressed', None)
turnscr('the module busy', sewboth(sewpress(0x01), b8(0x00114DB9, 0x01)))
turnscr('the module busy on the link',
        sewboth(sewpress(0x01), b8(0x00114D50, 0x20)))

# Box one, the turn.
turnscr('the turn', sewpress(0x01))
turnscr('the turn, the other running state',
        sewboth(sewpress(0x01), b8(0x00FFFEC0, 0x06)))
turnscr('the turn, the machine not running',
        sewboth(sewpress(0x01), b8(0x00FFFEC0, 0x05)))
turnscr('the turn, the other code',
        sewboth(sewpress(0x01), b8(0x00114DB4, 0x01)))
turnscr('the turn, the link busy',
        sewboth(sewpress(0x01), b8(0x0011F29E, 0x01)))
turnscr('the turn, the count already up',
        sewboth(sewpress(0x01), b8(0x00114DB3, 0xFF)))
turnscr('the turn, bits already in the byte',
        sewboth(sewpress(0x01), b8(0x0011A618, 0xFF)))

# Box two, the mirror.
turnscr('the mirror', sewpress(0x02))
turnscr('the mirror, the other code',
        sewboth(sewpress(0x02), b8(0x00114DB4, 0x01)))
turnscr('the mirror, the machine not running',
        sewboth(sewpress(0x02), b8(0x00FFFEC0, 0x00)))
turnscr('the mirror, the link busy',
        sewboth(sewpress(0x02), b8(0x0011F2B6, 0x01)))
turnscr('the mirror, bits already in the byte',
        sewboth(sewpress(0x02), b8(0x0011A618, 0xFF)))

# Box three, the way out.
turnscr('the way out', sewpress(0x03, 0x19))
turnscr('the way out, something already asked for',
        sewboth(sewpress(0x03, 0x19), b8(0x00114D72, 0x01)))
turnscr('the way out, the link busy',
        sewboth(sewpress(0x03, 0x19), b8(0x0011F29E, 0x01)))

# A box whose value is none of the three the routine knows.
turnscr('a value the table does not name', sewpress(0x03))
turnscr('a value of nought', sewpress(0x02, 0x00))

# H'225A3E
for nm, arrived, relayout, ex in (('just arrived', 1, 0, None),
                                  ('laid out again', 0, 1, None),
                                  ('a plain pass', 0, 0, None),
                                  ('a press on the turn', 0, 0,
                                   dict(list(b8(0x00FFFED9, 0x0C).items()) +
                                        list(b8(0x00FFFEDA, 0x24).items()) +
                                        list(boxval(1, 1).items())))):
    e = pat_extra(ex)
    dispatch_case(nm, 0x24, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['steps'] = 60000000
    new[-1]['name'] = 'screen_body_24 (%s)' % nm


# ------------------------------------------ screen H'23's own press, and its
# four helpers. Eleven boxes, and every one of them can be reached: the two
# that send do not wait afterwards.
def panelscr(nm, ex, steps=400000000):
    e = collections.OrderedDict()
    e.update(b8(0x00FFFEC0, 0x04))
    e.update(b8(0x00FFFEC1, 0x00))
    e.update(b8(0x00FFFEC6, 0x00))
    e.update(b8(0x00114D4F, 0x00))
    e.update(b8(0x00114D50, 0x00))
    e.update(b8(0x00114D62, 0x0D))
    e.update(b8(0x00114D65, 0x00))
    e.update(b8(0x00114D66, 0x00))
    e.update(b8(0x00114D72, 0x00))
    e.update(b8(0x00114D7D, 0x00))
    e.update(b8(0x00114D7E, 0x00))
    e.update(b8(0x00114D80, 0x00))
    e.update(b8(0x00114D89, 0x02))
    e.update(b8(0x00114D8A, 0x00))
    e.update(b8(0x00114D8C, 0x36))
    e.update(b8(0x00114D8D, 0x05))
    e.update(b8(0x00114D93, 0x00))
    e.update(b8(0x00114D94, 0x00))
    e.update(b8(0x00114D95, 0x00))
    e.update(b8(0x00114D96, 0x00))
    e.update(b8(0x00114D98, 0x00))
    e.update(b8(0x00114D9B, 0x00))
    e.update(b8(0x00114D9C, 0x00))
    e.update(b8(0x00114D9F, 0x00))
    e.update(b8(0x00114DA1, 0x00))
    e.update(b8(0x00114DAC, 0x00))
    e.update(b8(0x00114DAD, 0x00))
    e.update(b8(0x00114DB9, 0x00))
    e.update(b8(0x00114DBE, 0x20))
    e.update(b8(0x00114DBF, 0x50))
    e.update(b8(0x00114DC0, 0x60))
    e.update(b8(0x0011A618, 0x00))
    e.update(b8(0x0011A61A, 0x00))
    e.update(b8(0x0011F29E, 0x00))
    e.update(b8(0x0011F2A1, 0x00))
    e.update(b8(0x0011F2B6, 0x00))
    e.update(b8(0x0011F30E, 0x00))
    e.update(b8(0x0011F538, 0x00))
    # The screen slots and the "just arrived" flag: H'21F09E takes a short
    # path home when the slot already holds the screen being asked for, and
    # left unpinned the two images do not agree about what the slots hold.
    e.update(b8(0x0011A169, 0x23))
    e.update(b8(0x0011A16A, 0x00))
    e.update(b8(0x0011A16B, 0x00))
    e.update(b8(0x0011A16C, 0x00))
    e.update(b8(0x0011A16D, 0x00))
    e.update(b8(0x0011A174, 0x00))
    e.update(b8(0x0011A176, 0x00))
    e.update(b8(0x0011B0A8, 0x00))
    e.update(b8(0x00114DC6, 0x00))
    if ex: e.update(ex)
    mod3("module_panel_screen (%s)" % nm, {'addr': '22F962'},
         {'symbol': '_module_panel_screen'}, extra=e, steps=steps, pin=e)

panelscr('nothing pressed', None)
panelscr('the module busy', sewboth(sewpress(0x01), b8(0x00114DB9, 0x01)))
panelscr('a value past the table', sewpress(0x01, 0x0C))

# Box one, which starts the sewing.
panelscr('the start', sewpress(0x01))
panelscr('the start, the link lost',
         sewboth(sewpress(0x01), b8(0x00114DAC, 0x01)))
panelscr('the start, a reset wanted',
         sewboth(sewpress(0x01),
                 dict(list(b8(0x00114D9F, 0x01).items()) +
                      list(b8(0x00114D9C, 0x00).items()))))
panelscr('the start, a reset part way through',
         sewboth(sewpress(0x01),
                 dict(list(b8(0x00114D9F, 0x01).items()) +
                      list(b8(0x00114D9C, 0x01).items()))))
panelscr('the start, the reset walk on its own',
         sewboth(sewpress(0x01), b8(0x00114D9C, 0x01)))
panelscr('the start, the hoop the other way',
         sewboth(sewpress(0x01), b8(0x00114DA1, 0x01)))
panelscr('the start, the machine not at rest',
         sewboth(sewpress(0x01), b8(0x00FFFEC6, 0x02)))
panelscr('the start, already reported',
         sewboth(sewpress(0x01), b8(0x00114D9B, 0x01)))
panelscr('the start, something to report',
         sewboth(sewpress(0x01), b8(0x00114D51, 0x40)))
panelscr('the start, a stop asked for',
         sewboth(sewpress(0x01), b8(0x0011F534, 0x01)))
panelscr('the start, the count a whole turn of the wheel',
         sewboth(sewpress(0x01), b8(0x00114D8C, 0x1B)))

# Boxes two and three, the colour stepped on and back.
panelscr('a colour on', sewpress(0x02))
panelscr('a colour on, the machine not running',
         sewboth(sewpress(0x02), b8(0x00FFFEC0, 0x00)))
panelscr('a colour on, paused',
         sewboth(sewpress(0x02), b8(0x0011F30E, 0x01)))
panelscr('a colour on, the ask already in',
         sewboth(sewpress(0x02), b8(0x00114D4F, 0x01)))
panelscr('a colour on, the ask in at the last colour',
         sewboth(sewpress(0x02),
                 dict(list(b8(0x00114D4F, 0x01).items()) +
                      list(b8(0x00114D89, 0x04).items()))))
panelscr('a colour on, the ask in past the count',
         sewboth(sewpress(0x02),
                 dict(list(b8(0x00114D4F, 0x01).items()) +
                      list(b8(0x00114D89, 0x3C).items()))))
panelscr('a colour on, the link busy',
         sewboth(sewpress(0x02), b8(0x0011F29E, 0x01)))
panelscr('a colour back', sewpress(0x03))
panelscr('a colour back, the ask already in',
         sewboth(sewpress(0x03), b8(0x00114D4F, 0x02)))
panelscr('a colour back, the ask in at the first colour',
         sewboth(sewpress(0x03),
                 dict(list(b8(0x00114D4F, 0x02).items()) +
                      list(b8(0x00114D89, 0x00).items()))))
panelscr('a colour back, both asks in',
         sewboth(sewpress(0x03), b8(0x00114D4F, 0x03)))
panelscr('a colour back, paused',
         sewboth(sewpress(0x03), b8(0x0011F30E, 0x01)))

# Box four, the turning screen.
panelscr('the turning screen', sewpress(0x04))
panelscr('the turning screen, the machine not at rest',
         sewboth(sewpress(0x04), b8(0x00FFFEC6, 0x02)))
panelscr('the turning screen, paused',
         sewboth(sewpress(0x04), b8(0x0011F30E, 0x01)))

# Box five, the needle toggled.
panelscr('the needle', sewpress(0x05))
panelscr('the needle, already down',
         sewboth(sewpress(0x05), b8(0x00114D96, 0x01)))
panelscr('the needle, the module going home',
         sewboth(sewpress(0x05), b8(0x00114D66, 0x01)))

# Boxes six and eight, the two ways out, and box seven, the pause.
panelscr('the first way out', sewpress(0x06))
panelscr('the first way out, the link lost',
         sewboth(sewpress(0x06), b8(0x00114DAC, 0x01)))
panelscr('the first way out, the machine not at rest',
         sewboth(sewpress(0x06), b8(0x00FFFEC6, 0x02)))
panelscr('the first way out, the link busy',
         sewboth(sewpress(0x06), b8(0x0011F2B6, 0x01)))
panelscr('the pause', sewpress(0x07))
panelscr('the pause, already paused',
         sewboth(sewpress(0x07), b8(0x0011F30E, 0x01)))
panelscr('the second way out', sewpress(0x08))
panelscr('the second way out, the machine not at rest',
         sewboth(sewpress(0x08), b8(0x00FFFEC6, 0x02)))
panelscr('the second way out, the link busy',
         sewboth(sewpress(0x08), b8(0x0011F2B6, 0x01)))
panelscr('the second way out, the link busy and box ten live',
         sewboth(sewpress(0x08),
                 dict(list(b8(0x0011F2B6, 0x01).items()) +
                      list(b8(0x0011F538, 0x01).items()))))

# Box ten, screen H'4E.
panelscr('box ten', sewpress(0x0A))
panelscr('box ten, live', sewboth(sewpress(0x0A), b8(0x0011F538, 0x01)))
panelscr('box ten, the module going home',
         sewboth(sewpress(0x0A), b8(0x00114D66, 0x01)))

# Boxes nine and eleven, the speed walked down and up.
panelscr('the speed down', sewpress(0x09))
panelscr('the speed down, already at the bottom',
         sewboth(sewpress(0x09),
                 dict(list(b8(0x00114DBE, 0x12).items()) +
                      list(b8(0x00114DBF, 0x38).items()) +
                      list(b8(0x00114DC0, 0xC8).items()))))
panelscr('the speed down, one step from the bottom',
         sewboth(sewpress(0x09),
                 dict(list(b8(0x00114DBE, 0x13).items()) +
                      list(b8(0x00114DBF, 0x39).items()) +
                      list(b8(0x00114DC0, 0xC7).items()))))
panelscr('the speed down, only the first byte at its limit',
         sewboth(sewpress(0x09), b8(0x00114DBE, 0x12)))
panelscr('the speed up', sewpress(0x0B))
panelscr('the speed up, already at the top',
         sewboth(sewpress(0x0B),
                 dict(list(b8(0x00114DBE, 0x3E).items()) +
                      list(b8(0x00114DBF, 0xFA).items()) +
                      list(b8(0x00114DC0, 0x19).items()))))
panelscr('the speed up, one step from the top',
         sewboth(sewpress(0x0B),
                 dict(list(b8(0x00114DBE, 0x3D).items()) +
                      list(b8(0x00114DBF, 0xF9).items()) +
                      list(b8(0x00114DC0, 0x1A).items()))))
panelscr('the speed up, only the first byte at its limit',
         sewboth(sewpress(0x0B), b8(0x00114DBE, 0x3E)))
panelscr('the speed up, only the second byte at its limit',
         sewboth(sewpress(0x0B), b8(0x00114DBF, 0xFA)))
panelscr('the speed up, only the third byte at its limit',
         sewboth(sewpress(0x0B), b8(0x00114DC0, 0x19)))

# The four routines screen H'23 brought with it. Two of them the press
# cannot exercise: bits 0 and 1 of H'114D4F are among the things that make
# the module busy on screen four, so an arm that reaches the colour-step
# service always finds both bits down and nothing to do.
def helper(nm, orig, rebuilt, ex, steps=400000000):
    e = collections.OrderedDict()
    # The colour records at H'104D4A, not the picking grid's thumbnails that
    # the module-screen fill puts there: the colour step draws the colour's
    # own picture, and a record the fill does not pin sends the unpack off
    # into memory the two images do not agree about -- which is a hang, not
    # a difference.
    e.update(COLOUR_RECORDS)
    e.update(b8(0x00114D4F, 0x00))
    # Only records nought to three are pinned, and the colour step draws the
    # record it lands on, so every case has to leave H'114D89 under four
    # once the step it takes has been taken.
    e.update(b8(0x00114D89, 0x02))
    e.update(b8(0x00114D8D, 0x04))
    e.update(b8(0x00114D8E, 0x00))
    e.update(b8(0x00114D9C, 0x00))
    e.update(b8(0x00114D9F, 0x00))
    e.update(b8(0x00114D80, 0x00))
    e.update(b8(0x00114D7E, 0x00))
    e.update(b8(0x00114D83, 0x00))
    e.update(b8(0x00114DA1, 0x00))
    e.update(b8(0x00FFFEC6, 0x00))
    e.update(b8(0x0011F29E, 0x00))
    e.update(b8(0x0011F2B6, 0x00))
    e.update(b8(0x0011F538, 0x00))
    e.update(w16(0x0011F4DC, 0xAAAA))
    e.update(w16(0x0011F4DE, 0xAAAA))
    # H'114D98 down, so that H'236E9A's cursor erase leaves the two words
    # above alone: they are here to be seen being put to nought by the
    # colour step, not to be fetched back over by a region copy whose
    # corners H'AAAA puts halfway across the machine.
    e.update(b8(0x00114D98, 0x00))
    e.update(b8(0x00114D99, 0x00))
    # The screen slots and the "just arrived" flag: H'21F09E takes a short
    # path home when the slot already holds the screen being asked for, and
    # left unpinned the two images do not agree about what the slots hold.
    e.update(b8(0x0011A169, 0x23))
    e.update(b8(0x0011A16A, 0x00))
    e.update(b8(0x0011A16B, 0x00))
    e.update(b8(0x0011A16C, 0x00))
    e.update(b8(0x0011A16D, 0x00))
    e.update(b8(0x0011A174, 0x00))
    e.update(b8(0x0011A176, 0x00))
    e.update(b8(0x0011B0A8, 0x00))
    e.update(b8(0x00114DC6, 0x00))
    if ex: e.update(ex)
    mod3(nm, orig, rebuilt, extra=e, steps=steps, pin=e)

# H'23E464
for bit, nm in ((0x00, 'the box not offered'), (0x01, 'the box offered'),
                (0xFE, 'every bit but the one it reads')):
    helper('module_box10_live (%s)' % nm,
           {'addr': '23E464', 'result': 'r6l'},
           {'symbol': '_module_box10_live', 'result': 'r0l'},
           b8(0x0011F538, bit))

# H'239FAA
helper('module_colours_dither (the checkerboard)', {'addr': '239FAA'},
       {'symbol': '_module_colours_dither'}, None)

# H'2385A6
def stepsvc(nm, ex):
    helper('module_colour_step_service (%s)' % nm, {'addr': '2385A6'},
           {'symbol': '_module_colour_step_service'}, ex)

stepsvc('neither ask in', None)
stepsvc('the colour on', b8(0x00114D4F, 0x01))
stepsvc('the colour on, at the last colour',
        dict(list(b8(0x00114D4F, 0x01).items()) +
             list(b8(0x00114D89, 0x03).items())))
stepsvc('the colour on, from the first',
        dict(list(b8(0x00114D4F, 0x01).items()) +
             list(b8(0x00114D89, 0x00).items())))
stepsvc('the colour on, past the count',
        dict(list(b8(0x00114D4F, 0x01).items()) +
             list(b8(0x00114D89, 0x3C).items())))
stepsvc('the colour on, exactly at the count',
        dict(list(b8(0x00114D4F, 0x01).items()) +
             list(b8(0x00114D89, 0x3B).items()) +
             list(b8(0x00114D8D, 0x3C).items())))
stepsvc('the colour on, no colours at all',
        dict(list(b8(0x00114D4F, 0x01).items()) +
             list(b8(0x00114D8D, 0x00).items())))
stepsvc('the colour on, the arrow lit',
        dict(list(b8(0x00114D4F, 0x01).items()) +
             list(b8(0x00114D8E, 0x04).items())))
stepsvc('the colour on, at the count with colours beyond',
        dict(list(b8(0x00114D4F, 0x01).items()) +
             list(b8(0x00114D89, 0x3C).items()) +
             list(b8(0x00114D8D, 0x3E).items())))
stepsvc('the colour back', b8(0x00114D4F, 0x02))
stepsvc('the colour back, at the first colour',
        dict(list(b8(0x00114D4F, 0x02).items()) +
             list(b8(0x00114D89, 0x00).items())))
stepsvc('the colour back, the arrow lit',
        dict(list(b8(0x00114D4F, 0x02).items()) +
             list(b8(0x00114D8E, 0x04).items())))
stepsvc('both asks in', b8(0x00114D4F, 0x03))
stepsvc('both asks in, the arrows lit',
        dict(list(b8(0x00114D4F, 0x03).items()) +
             list(b8(0x00114D8E, 0x04).items())))

# H'231BB0
def resetwalk(nm, ex):
    helper('module_reset_walk (%s)' % nm, {'addr': '231BB0'},
           {'symbol': '_module_reset_walk'}, ex)

resetwalk('no reset wanted', None)
resetwalk('the whole walk', b8(0x00114D9F, 0x01))
resetwalk('the second step on its own',
          dict(list(b8(0x00114D9F, 0x01).items()) +
               list(b8(0x00114D9C, 0x01).items())))
resetwalk('the whole walk, a bit already down',
          dict(list(b8(0x00114D9F, 0x01).items()) +
               list(b8(0x00114D4F, 0x08).items())))
resetwalk('the whole walk, the slot further along',
          dict(list(b8(0x00114D9F, 0x01).items()) +
               list(b8(0x0011A660, 0x03).items())))

# H'225998
for nm, arrived, relayout, ex in (('just arrived', 1, 0, None),
                                  ('laid out again', 0, 1, None),
                                  ('a plain pass', 0, 0, None),
                                  ('a press on the needle', 0, 0,
                                   dict(list(b8(0x00FFFED9, 0x4C).items()) +
                                        list(b8(0x00FFFEDA, 0x24).items()) +
                                        list(boxval(5, 5).items())))):
    e = pat_extra(ex)
    dispatch_case(nm, 0x23, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['steps'] = 60000000
    new[-1]['name'] = 'screen_body_23 (%s)' % nm


# ------------------------------------------------- screen H'4E's own press
# Twenty-five values and twenty-five empty arms, so the cases are about the
# three things that happen before the table: the screen state marked, the
# held message shown, and the value range. H'24610A answers "not busy" for
# every state past H'0B and this screen is H'10, so the busy question can
# never turn the routine back.
def extrascr(nm, ex, steps=400000000):
    e = collections.OrderedDict()
    e.update(b8(0x00114D8E, 0x00))
    e.update(b8(0x00114DB9, 0x00))
    e.update(b8(0x00114D50, 0x00))
    e.update(b8(0x0011F29E, 0x00))
    e.update(b8(0x0011F2B6, 0x00))
    if ex: e.update(ex)
    mod3("module_extra_screen (%s)" % nm, {'addr': '22F82A'},
         {'symbol': '_module_extra_screen'}, extra=e, steps=steps, pin=e)

extrascr('nothing pressed', None)
for b in (0x01, 0x02, 0x0C, 0x17):
    extrascr('the box at %02X' % b, sewpress(b))
extrascr('a box whose value is the last the table covers',
         sewpress(0x01, 0x19))
extrascr('a box one past the table', sewpress(0x01, 0x1A))
extrascr('a box whose value is nought', sewpress(0x02, 0x00))
extrascr('a box that is not its own number', sewpress(0x03, 0x11))
# H'114DB9 is what makes H'24610A say busy on the states it knows; on this
# one it changes nothing, which is the point.
extrascr('the module claiming to be busy',
         sewboth(sewpress(0x01), b8(0x00114DB9, 0x01)))

# H'2258F2
for nm, arrived, relayout, ex in (('just arrived', 1, 0, None),
                                  ('laid out again', 0, 1, None),
                                  ('a plain pass', 0, 0, None),
                                  ('a press on the first box', 0, 0,
                                   dict(list(b8(0x00FFFED9, 0x0C).items()) +
                                        list(b8(0x00FFFEDA, 0x24).items()) +
                                        list(boxval(1, 1).items())))):
    e = pat_extra(ex)
    dispatch_case(nm, 0x4E, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['steps'] = 60000000
    new[-1]['name'] = 'screen_body_4E (%s)' % nm


# ------------------------------------ two of screen H'16's helpers
# H'230EF4 is H'230EA8 over again, byte for byte, so it gets the same cases.
for _a, _sym in (('230EA8', '_embroidery_panel_save'),
                 ('230EF4', '_embroidery_panel_save_b')):
    pat_add('%s (nothing to put away)' % _sym[1:], {'addr': _a},
            {'symbol': _sym}, extra=b8(0x0011F4E6, 0x00), steps=60000000)
    pat_add('%s (the panel put away)' % _sym[1:], {'addr': _a},
            {'symbol': _sym}, extra=b8(0x0011F4E6, 0x01), steps=60000000)

# H'23A06A
def shrink(nm, ex):
    e = collections.OrderedDict()
    e.update(b8(0x0011A615, 0x00))
    e.update(b8(0x0011F2A2, 0xFF))
    e.update(b8(0x0011F4D9, 0x32))
    e.update(b8(0x0011F4DA, 0x32))
    e.update(b8(0x0011A25A + 0x10*MOD_SLOT, 0x32))
    e.update(b8(0x0011A25B + 0x10*MOD_SLOT, 0x32))
    e.update(w16(0x0011A266 + 0x10*MOD_SLOT, 0x0001))
    if ex: e.update(ex)
    mod3('module_size_shrink (%s)' % nm, {'addr': '23A06A'},
         {'symbol': '_module_size_shrink'}, extra=e, pin=e)

shrink('the slot no smaller', None)
shrink('the slot narrower', b8(0x0011A25A + 0x10*MOD_SLOT, 0x20))
shrink('the slot shorter', b8(0x0011A25B + 0x10*MOD_SLOT, 0x20))
shrink('the slot smaller both ways',
       dict(list(b8(0x0011A25A + 0x10*MOD_SLOT, 0x20).items()) +
            list(b8(0x0011A25B + 0x10*MOD_SLOT, 0x28).items())))
shrink('the slot narrower but not one that can be measured',
       dict(list(b8(0x0011A25A + 0x10*MOD_SLOT, 0x20).items()) +
            list(w16(0x0011A266 + 0x10*MOD_SLOT, 0x0000).items())))
shrink('the slot one under in width',
       b8(0x0011A25A + 0x10*MOD_SLOT, 0x31))
shrink('the slot one over in width',
       b8(0x0011A25A + 0x10*MOD_SLOT, 0x33))
shrink('the slot narrower, another slot',
       dict(list(b8(0x0011A660, 0x03).items()) +
            list(b8(0x0011A25A + 0x10*0x03, 0x20).items()) +
            list(b8(0x0011A25B + 0x10*0x03, 0x32).items()) +
            list(w16(0x0011A266 + 0x10*0x03, 0x0001).items())))


# H'245CE6. Six ways to answer yes and one to answer no, so seven cases and
# a couple either side of the boundaries.
def slotchg(nm, ex):
    e = collections.OrderedDict()
    e.update(b8(0x00114D73, 0x00))
    e.update(b8(0x0011A63C, 0x00))
    e.update(b8(0x0011A25A + 0x10*MOD_SLOT, 0x32))
    e.update(b8(0x0011A25B + 0x10*MOD_SLOT, 0x33))
    e.update(b8(0x0011A25F + 0x10*MOD_SLOT, 0x34))
    e.update(b8(0x0011A260 + 0x10*MOD_SLOT, 0x35))
    e.update(b8(0x0011F310, 0x32))
    e.update(b8(0x0011F311, 0x33))
    e.update(b8(0x0011F312, 0x34))
    e.update(b8(0x0011F313, 0x35))
    e.update(b8(0x0011F314, MOD_SLOT))
    if ex: e.update(ex)
    mod3('module_slot_changed (%s)' % nm,
         {'addr': '245CE6', 'result': 'r6l'},
         {'symbol': '_module_slot_changed', 'result': 'r0l'},
         extra=e, pin=e)

slotchg('nothing moved', None)
slotchg('the flag up on its own', b8(0x00114D73, 0x01))
slotchg('the first byte moved', b8(0x0011A25A + 0x10*MOD_SLOT, 0x40))
slotchg('the second byte moved', b8(0x0011A25B + 0x10*MOD_SLOT, 0x40))
slotchg('the third byte moved', b8(0x0011A25F + 0x10*MOD_SLOT, 0x40))
slotchg('the fourth byte moved', b8(0x0011A260 + 0x10*MOD_SLOT, 0x40))
slotchg('the slot itself moved', b8(0x0011F314, 0x00))
slotchg('the copy behind on every byte',
        dict(list(b8(0x0011F310, 0x00).items()) +
             list(b8(0x0011F311, 0x00).items()) +
             list(b8(0x0011F312, 0x00).items()) +
             list(b8(0x0011F313, 0x00).items())))
slotchg('the bit already up', b8(0x0011A63C, 0xFF))
slotchg('another slot, nothing moved',
        dict(list(b8(0x0011A660, 0x03).items()) +
             list(b8(0x0011F314, 0x03).items()) +
             list(b8(0x0011A25A + 0x10*0x03, 0x32).items()) +
             list(b8(0x0011A25B + 0x10*0x03, 0x33).items()) +
             list(b8(0x0011A25F + 0x10*0x03, 0x34).items()) +
             list(b8(0x0011A260 + 0x10*0x03, 0x35).items())))
slotchg('another slot, a byte moved',
        dict(list(b8(0x0011A660, 0x03).items()) +
             list(b8(0x0011F314, 0x03).items()) +
             list(b8(0x0011A25A + 0x10*0x03, 0x32).items()) +
             list(b8(0x0011A25B + 0x10*0x03, 0x99).items()) +
             list(b8(0x0011A25F + 0x10*0x03, 0x34).items()) +
             list(b8(0x0011A260 + 0x10*0x03, 0x35).items())))


# ------------------------------------- screen H'16's own press, and its body
# Twenty-five values over six numbers. Values five to seven are turned back
# before the table, so the three arms they point at cannot be reached at all.
def sizescr(nm, ex, steps=400000000):
    e = collections.OrderedDict()
    e.update(b8(0x00114D8E, 0x00))
    e.update(b8(0x00114D50, 0x00))
    e.update(b8(0x00114D55, 0x00))
    e.update(b8(0x00114D5D, 0x00))
    e.update(b8(0x00114D5E, 0x00))
    e.update(b8(0x00114D66, 0x00))
    e.update(b8(0x00114D67, 0x00))
    e.update(b8(0x00114D72, 0x00))
    e.update(b8(0x00114D73, 0x00))
    e.update(b8(0x00114D7E, 0x00))
    e.update(b8(0x00114D89, 0x00))
    e.update(b8(0x00114D8C, 0x36))
    e.update(b8(0x00114D93, 0x00))
    e.update(b8(0x00114D97, 0x00))
    e.update(b8(0x00114D9B, 0x00))
    e.update(b8(0x00114D9F, 0x00))
    e.update(b8(0x00114DA1, 0x00))
    e.update(b8(0x00114DAD, 0x00))
    e.update(b8(0x00114DB9, 0x00))
    e.update(b8(0x0011A619, 0x00))
    e.update(b8(0x0011A63C, 0x00))
    e.update(b8(0x0011A640, 0x00))
    e.update(b8(0x0011F299, 0x00))
    e.update(b8(0x0011F29A, 0x00))
    e.update(b8(0x0011F29E, 0x00))
    e.update(b8(0x0011F2A1, 0x00))
    e.update(b8(0x0011F2B6, 0x00))
    e.update(b8(0x0011F4E6, 0x00))
    # the six numbers, all at their middle
    e.update(b8(0x0011A25A + 0x10*MOD_SLOT, 0x32))
    e.update(b8(0x0011A25B + 0x10*MOD_SLOT, 0x32))
    e.update(b8(0x0011A25C + 0x10*MOD_SLOT, 0x32))
    e.update(b8(0x0011A25D + 0x10*MOD_SLOT, 0x32))
    e.update(b8(0x0011A261 + 0x10*MOD_SLOT, 0x32))
    e.update(b8(0x0011A262 + 0x10*MOD_SLOT, 0x32))
    e.update(b8(0x0011A264 + 0x10*MOD_SLOT, 0x28))
    if ex: e.update(ex)
    mod3("module_sizes_screen (%s)" % nm, {'addr': '22DBFA'},
         {'symbol': '_module_sizes_screen'}, extra=e, steps=steps, pin=e)

sizescr('nothing pressed', None)
sizescr('the module busy', sewboth(sewpress(0x02), b8(0x00114DB9, 0x01)))
sizescr('a value past the table', sewpress(0x01, 0x1A))
for v in (0x05, 0x06, 0x07):
    sizescr('the value at %02X, which turns back first' % v,
            sewpress(0x01, v))

# The two percentages: down, back to the middle, and up, on each.
for v, nm in ((0x02, 'the first width down'), (0x03, 'the first width back'),
              (0x04, 'the first width up'), (0x09, 'the second width down'),
              (0x0A, 'the second width back'), (0x0B, 'the second width up')):
    sizescr(nm, sewpress(0x01, v))
sizescr('the first width down at its floor',
        sewboth(sewpress(0x01, 0x02), b8(0x0011A25A + 0x10*MOD_SLOT, 0x0A)))
sizescr('the first width down one above its floor',
        sewboth(sewpress(0x01, 0x02), b8(0x0011A25A + 0x10*MOD_SLOT, 0x0B)))
sizescr('the first width up at its ceiling',
        sewboth(sewpress(0x01, 0x04), b8(0x0011A25A + 0x10*MOD_SLOT, 0x64)))
sizescr('the first width up one below its ceiling',
        sewboth(sewpress(0x01, 0x04), b8(0x0011A25A + 0x10*MOD_SLOT, 0x63)))
sizescr('the second width down at its floor',
        sewboth(sewpress(0x01, 0x09), b8(0x0011A25B + 0x10*MOD_SLOT, 0x0A)))
sizescr('a width key with the state bit up',
        sewboth(sewpress(0x01, 0x02), b8(0x00114D55, 0x02)))
sizescr('a width key with the other state bit up',
        sewboth(sewpress(0x01, 0x02), b8(0x00114D55, 0x04)))
sizescr('a width key with a reset wanted',
        sewboth(sewpress(0x01, 0x02), b8(0x00114D9F, 0x01)))
sizescr('a width key with the module going home',
        sewboth(sewpress(0x01, 0x02), b8(0x00114D66, 0x01)))
sizescr('the middle key with the state bit up, which it does not look at',
        sewboth(sewpress(0x01, 0x03), b8(0x00114D55, 0x02)))

# The two pairs.
for v, nm in ((0x0D, 'the first pair down'), (0x0E, 'the first pair back'),
              (0x0F, 'the first pair up'), (0x10, 'the second pair down'),
              (0x11, 'the second pair back'), (0x12, 'the second pair up')):
    sizescr(nm, sewpress(0x01, v))
sizescr('the first pair down at its floor',
        sewboth(sewpress(0x01, 0x0D),
                dict(list(b8(0x0011A25C + 0x10*MOD_SLOT, 0x01).items()) +
                     list(b8(0x0011A25D + 0x10*MOD_SLOT, 0x01).items()))))
sizescr('the first pair down with only the second at its floor',
        sewboth(sewpress(0x01, 0x0D), b8(0x0011A25D + 0x10*MOD_SLOT, 0x01)))
sizescr('the first pair down with the number at its bound',
        sewboth(sewpress(0x01, 0x0D), b8(0x0011A25C + 0x10*MOD_SLOT, 0x0A)))
sizescr('the first pair up at its ceiling',
        sewboth(sewpress(0x01, 0x0F),
                dict(list(b8(0x0011A25C + 0x10*MOD_SLOT, 0xC8).items()) +
                     list(b8(0x0011A25D + 0x10*MOD_SLOT, 0xC8).items()))))
sizescr('the first pair up with the number at its bound',
        sewboth(sewpress(0x01, 0x0F), b8(0x0011A25C + 0x10*MOD_SLOT, 0xC5)))
sizescr('the second pair down at its floor',
        sewboth(sewpress(0x01, 0x10),
                dict(list(b8(0x0011A261 + 0x10*MOD_SLOT, 0x01).items()) +
                     list(b8(0x0011A262 + 0x10*MOD_SLOT, 0x01).items()))))
sizescr('a pair key with the link busy',
        sewboth(sewpress(0x01, 0x0D), b8(0x0011F29E, 0x01)))

# The speed.
for v, nm in ((0x13, 'the speed down'), (0x14, 'the speed back'),
              (0x15, 'the speed up')):
    sizescr(nm, sewpress(0x01, v))

# The two ways out, the redraw, and the start.
sizescr('the first way out', sewpress(0x01, 0x08))
sizescr('the first way out, something already asked for',
        sewboth(sewpress(0x01, 0x08), b8(0x00114D72, 0x01)))
sizescr('the first way out, with a panel to put away',
        sewboth(sewpress(0x01, 0x08), b8(0x0011F4E6, 0x01)))
sizescr('the second way out', sewpress(0x01, 0x19))
sizescr('the second way out, something already asked for',
        sewboth(sewpress(0x01, 0x19), b8(0x00114D72, 0x01)))
sizescr('the redraw', sewpress(0x01, 0x0C))
sizescr('the redraw, the module going home',
        sewboth(sewpress(0x01, 0x0C), b8(0x00114D66, 0x01)))
sizescr('the redraw, the slot moved',
        sewboth(sewpress(0x01, 0x0C), b8(0x00114D73, 0x01)))
sizescr('the start, a reset wanted',
        sewboth(sewpress(0x01, 0x01), b8(0x00114D9F, 0x01)))
sizescr('the start, nothing to send',
        sewboth(sewpress(0x01, 0x01), b8(0x0011A640, 0x00)))
sizescr('the start, the module going home',
        sewboth(sewpress(0x01, 0x01), b8(0x00114D66, 0x01)))

# H'22584C
for nm, arrived, relayout, ex in (('just arrived', 1, 0, None),
                                  ('laid out again', 0, 1, None),
                                  ('a plain pass', 0, 0, None),
                                  ('a press on the first box', 0, 0,
                                   dict(list(b8(0x00FFFED9, 0x0C).items()) +
                                        list(b8(0x00FFFEDA, 0x24).items()) +
                                        list(boxval(1, 1).items())))):
    e = pat_extra(ex)
    dispatch_case(nm, 0x16, arrived, relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['steps'] = 60000000
    new[-1]['name'] = 'screen_body_16 (%s)' % nm

# ------------------------------------ screen H'15's fit test
# H'245848. The design's diagonal swung to each side of the corner angle, and
# both corners asked to keep clear of the hoop on each axis -- four checks, so
# a case that turns back at each of them and a couple that get all the way
# through. The design H'104CCE and H'104D06 hold is cut down to a hundred by a
# hundred for most of these, which at the slot's own hundred per cent leaves a
# two-hundred by two-hundred design and a corner at forty-five degrees.
#
# Nothing sets H'11A25A to nought and nothing asks for a `turn' above one:
# both are paths where the ROM reads a local it never wrote, the same hole
# H'24217A has.
TURN_SMALL = dict(list(w16(0x00104CCE + 2*MOD_DESIGN, 0x0064).items()) +
                  list(w16(0x00104D06 + 2*MOD_DESIGN, 0x0064).items()))

def turnfit(nm, turn, up, by, ex=None):
    e = collections.OrderedDict()
    if ex: e.update(ex)
    mod3('module_turn_fits (%s)' % nm,
         {'addr': '245848', 'result': 'r6l',
          'regs': {'er6': '%02X' % turn},
          'stack': {'4': '2:%04X' % up, '6': '2:%04X' % by}},
         {'symbol': '_module_turn_fits', 'result': 'r0l',
          'regs': {'er0': '%02X' % turn, 'er1': '%02X' % up,
                   'er2': '%02X' % by}},
         extra=e, pin=e)

def turnsmall(nm, turn, up, by, ex=None):
    e = collections.OrderedDict(TURN_SMALL)
    if ex: e.update(ex)
    turnfit(nm, turn, up, by, e)

# The design the fill hands over is a five-hundred by six-hundred one, whose
# diagonal runs past the hoop's own thousand before it is turned at all.
turnfit('the design too big for the hoop', 0, 0, 0)
turnfit('the design too big, turned', 1, 1, 6)

turnsmall('a design that fits', 0, 0, 0)
turnsmall('turned down six', 1, 0, 6)
turnsmall('turned up six', 1, 1, 6)
turnsmall('turned neither way', 1, 2, 6)
turnsmall('the rotation the slot holds', 0, 0, 6)
turnsmall('turned right round',
          0, 0, 0, b8(0x0011A260 + 0x10*MOD_SLOT, 0x48))
turnsmall('mirrored', 0, 0, 0, b8(0x0011A25F + 0x10*MOD_SLOT, 0x01))
turnsmall('mirrored and turned', 1, 1, 6,
          b8(0x0011A25F + 0x10*MOD_SLOT, 0x01))

# The centre's own offset counts in tenths, so a hundred takes a whole
# thousand of the hoop away and nothing fits beside it.
turnsmall('the centre off to one side',
          0, 0, 0, w16(0x0011A266 + 0x10*MOD_SLOT, 0x0032))
turnsmall('the centre too far off to one side',
          0, 0, 0, w16(0x0011A266 + 0x10*MOD_SLOT, 0x0064))

# One case turning back at each of the four checks. The first two are the
# corner at plus the angle, the last two the corner at minus it, so a
# rotation of forty-five degrees puts one corner on an axis and the other
# off it and a narrow hoop can be made to catch either.
turnsmall('a hoop too short', 0, 0, 0, w16(0x0011A628, 0x0064))
turnsmall('the far corner out across',
          0, 0, 0, dict(list(b8(0x0011A260 + 0x10*MOD_SLOT, 0x2D).items()) +
                        list(w16(0x0011A626, 0x012C).items())))
turnsmall('the far corner out down',
          0, 0, 0, dict(list(b8(0x0011A260 + 0x10*MOD_SLOT, 0x1B).items()) +
                        list(w16(0x0011A628, 0x012C).items())))

# Either side of the edge, and on it.
for _lim, _nm in ((0x00DD, 'one under the edge'), (0x00DE, 'on the edge'),
                  (0x00DF, 'one over the edge')):
    turnsmall('the hoop %s' % _nm, 0, 0, 0, w16(0x0011A626, _lim))

# Another slot, with its own block and its own design.
_o = collections.OrderedDict(TURN_SMALL)
_o.update(b8(0x0011A660, 0x03))
for _k in range(0x12):
    _o.update(b8(0x0011A41A + 0x12*0x03 + _k, (0x21 + _k) & 0xFF))
_o.update(b8(0x0011A41A + 0x12*0x03, MOD_DESIGN))
_o.update(slotbytes(0x64, 0x64, 0x00, 0x24, slot=0x03))
_o.update(w16(0x0011A266 + 0x10*0x03, 0x0000))
_o.update(w16(0x0011A268 + 0x10*0x03, 0x0000))
turnfit('another slot', 0, 0, 0, _o)
turnfit('another slot, turned up six', 1, 1, 6, _o)

# The cases above leave the design a long way inside the hoop, where nothing
# much changes the answer. These put it right on the edge instead: a design
# that is not square, a rotation that is not nought, and a hoop cut down so
# that one step either way turns the answer over. Between them they cover
# each of the four checks, both scales, the diagonal, the corner angle, the
# two ways the rotation is stepped, the centre's offset in tenths and a
# centre that sits the other side of the middle.
def turntight(nm, turn, up, by, dw, dh, rot, lx, ly, px=0, mir=0):
    e = collections.OrderedDict()
    e.update(w16(0x00104CCE + 2*MOD_DESIGN, dw))
    e.update(w16(0x00104D06 + 2*MOD_DESIGN, dh))
    e.update(b8(0x0011A260 + 0x10*MOD_SLOT, rot))
    e.update(b8(0x0011A25F + 0x10*MOD_SLOT, mir))
    e.update(w16(0x0011A626, lx))
    e.update(w16(0x0011A628, ly))
    e.update(w16(0x0011A266 + 0x10*MOD_SLOT, px & 0xFFFF))
    turnfit(nm, turn, up, by, e)

turntight('a wide design just inside', 0, 0, 0, 200, 60, 0x1B, 400, 400)
turntight('a tall design just inside', 0, 0, 0, 60, 200, 0x1B, 400, 400)
turntight('a tall design in a tight hoop', 0, 0, 0, 60, 100, 0x24, 200, 300)
turntight('the near corner out across', 0, 0, 0, 60, 60, 0x1B, 300, 200, 20)
turntight('the near corner out across, mirrored',
          0, 0, 0, 60, 60, 0x1B, 300, 200, 20, 0x01)
turntight('six down brings it in', 1, 0, 6, 60, 100, 0x08, 200, 300)
turntight('six up brings it in', 1, 1, 6, 60, 100, 0x1B, 200, 300)
turntight('six up takes it out', 1, 1, 6, 60, 100, 0x08, 200, 300)
turntight('a turn that only the tall way clears',
          0, 0, 0, 60, 100, 0x23, 200, 300)
turntight('the centre just inside in tenths',
          0, 0, 0, 60, 100, 0x24, 246, 300, 10)
turntight('the centre the other side of the middle',
          0, 0, 0, 60, 100, 0x24, 230, 300, -10)

# ------------------------------------- screen H'15's own press, and its body
# Twenty-five values over twenty-five arms, so every one of them is reachable
# and every one gets at least a case. The design the fill hands over is too
# big for the hoop, which is what the size and turn keys have to cope with, so
# the ones that need a design that fits cut H'104CCE and H'104D06 down first.
def hoopscr(nm, ex, steps=400000000, small=False):
    e = collections.OrderedDict()
    for _a in (0x00114D50, 0x00114D55, 0x00114D5D, 0x00114D5E, 0x00114D66,
               0x00114D67, 0x00114D72, 0x00114D73, 0x00114D74, 0x00114D7E,
               0x00114D89, 0x00114D93, 0x00114D97, 0x00114D9B, 0x00114D9F,
               0x00114DA0, 0x00114DA1, 0x00114DAD, 0x00114DB0, 0x00114DB1,
               0x00114DB2, 0x00114DB5, 0x00114DB6, 0x00114DB9, 0x00114DBA,
               0x0011A612, 0x0011A614, 0x0011A615, 0x0011A619, 0x0011A63C,
               0x0011A640, 0x0011F299, 0x0011F29A, 0x0011F29E, 0x0011F2A1,
               0x0011F2A2, 0x0011F2B6, 0x0011F4E6):
        e.update(b8(_a, 0x00))
    e.update(b8(0x00114D8E, 0x00))
    e.update(b8(0x00114D8C, 0x36))
    e.update(w16(0x0011A63A, 0x0000))
    e.update(w16(0x0011F292, 0x0000))
    if small:
        e.update(TURN_SMALL)
    if ex: e.update(ex)
    mod3("module_hoop_screen (%s)" % nm, {'addr': '22C24C'},
         {'symbol': '_module_hoop_screen'}, extra=e, steps=steps, pin=e)

hoopscr('nothing pressed', None)
hoopscr('the module busy', sewboth(sewpress(0x02), b8(0x00114DB9, 0x01)))
hoopscr('a value past the table', sewpress(0x01, 0x1A))
hoopscr('a value of nought', sewpress(0x01, 0x00))
# The hit test's own bounds: a press on the last box it covers, and on the
# one before it, so narrowing the range has somewhere to show.
hoopscr('a press on the last box', sewpress(0x19))
hoopscr('a press on the box before the last', sewpress(0x18))

# H'01, the start. The message only goes out when the link is quiet and there
# is something to send, and the wait after it only ends on an interrupt, so
# the cases either have nothing to send or a busy link.
hoopscr('the start', sewpress(0x01))
hoopscr('the start, a reset wanted',
        sewboth(sewpress(0x01), b8(0x00114D9F, 0x01)))
hoopscr('the start, the module going home',
        sewboth(sewpress(0x01), b8(0x00114D66, 0x01)))
hoopscr('the start, something to send but a busy link',
        sewboth(sewpress(0x01),
                dict(list(b8(0x0011A640, 0x01).items()) +
                     list(b8(0x0011F29E, 0x01).items()))))
hoopscr('the start, going to the stitch screen',
        sewboth(sewpress(0x01), b8(0x00114DA1, 0x01)))
hoopscr('the start, the other flag already up',
        sewboth(sewpress(0x01), b8(0x00114D9B, 0x01)))
hoopscr('the start, something to report',
        sewboth(sewpress(0x01), b8(0x00114D51, 0x40)))
hoopscr('the start, a fault to report',
        sewboth(sewpress(0x01), b8(0x0011F534, 0x01)))

# H'02, H'03 and H'0D, the three bits in H'11A63A.
for _v, _nm in ((0x02, 'the first bit'), (0x03, 'the second bit'),
                (0x0D, 'the third bit')):
    hoopscr(_nm, sewpress(0x01, _v))
    hoopscr('%s, the module going home' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x00114D66, 0x01)))
    hoopscr('%s, a state bit up' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x00114D55, 0x02)))
    hoopscr('%s, the other state bit up' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x00114D55, 0x04)))
    hoopscr('%s, a reset wanted' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x00114D9F, 0x01)))
    hoopscr('%s, a busy link' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x0011F29E, 0x01)))
# Only the first and the third look at the fault report and at whether there
# is anything to report at all.
for _v, _nm in ((0x02, 'the first bit'), (0x03, 'the second bit'),
                (0x0D, 'the third bit')):
    hoopscr('%s, something to report' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x00114D51, 0x40)))
    hoopscr('%s, the bits already up' % _nm,
            sewboth(sewpress(0x01, _v), w16(0x0011A63A, 0xFFFF)))

# H'04, the mirror.
hoopscr('the mirror, off to on', sewpress(0x01, 0x04))
hoopscr('the mirror, on to off',
        sewboth(sewpress(0x01, 0x04), b8(0x0011A25F + 0x10*MOD_SLOT, 0x01)))
hoopscr('the mirror, a state bit up',
        sewboth(sewpress(0x01, 0x04), b8(0x00114D55, 0x02)))
hoopscr('the mirror, a reset wanted',
        sewboth(sewpress(0x01, 0x04), b8(0x00114D9F, 0x01)))
hoopscr('the mirror, a busy link',
        sewboth(sewpress(0x01, 0x04), b8(0x0011F29E, 0x01)))

# H'05 to H'07, the size. Both percentages move together; the two that make
# the design bigger ask H'244F14 first and put it back when it says no.
hoopscr('the size down', sewpress(0x01, 0x05))
hoopscr('the size down at its floor',
        sewboth(sewpress(0x01, 0x05), b8(0x0011A25A + 0x10*MOD_SLOT, 0x0A)))
hoopscr('the size down one above its floor',
        sewboth(sewpress(0x01, 0x05), b8(0x0011A25A + 0x10*MOD_SLOT, 0x0B)))
hoopscr('the size down with the second at its floor',
        sewboth(sewpress(0x01, 0x05), b8(0x0011A25B + 0x10*MOD_SLOT, 0x0A)))
hoopscr('the size down, a busy link',
        sewboth(sewpress(0x01, 0x05), b8(0x0011F29E, 0x01)))
hoopscr('the size back to a hundred, which will not fit',
        sewboth(sewpress(0x01, 0x06), w16(0x0011A626, 0x0064)))
hoopscr('the size back to a hundred, which fits',
        sewpress(0x01, 0x06), small=True)
hoopscr('the size back to a hundred, a state bit up it does not look at',
        sewboth(sewpress(0x01, 0x06), b8(0x00114D55, 0x02)), small=True)
hoopscr('the size back to a hundred, the module going home',
        sewboth(sewpress(0x01, 0x06), b8(0x00114D66, 0x01)))
hoopscr('the size back to a hundred, something to report',
        sewboth(sewpress(0x01, 0x06), b8(0x00114D51, 0x40)))
hoopscr('the size back to a hundred, a fault to report',
        sewboth(sewpress(0x01, 0x06), b8(0x0011F534, 0x01)))
hoopscr('the size down, something to report',
        sewboth(sewpress(0x01, 0x05), b8(0x00114D51, 0x40)))
hoopscr('the size down, a fault to report',
        sewboth(sewpress(0x01, 0x05), b8(0x0011F534, 0x01)))
# The fill leaves both percentages at a hundred, which is the ceiling the up
# key stops at, so the ones that mean to press it have to come down first.
def _half(ex=None):
    d = dict(list(b8(0x0011A25A + 0x10*MOD_SLOT, 0x32).items()) +
             list(b8(0x0011A25B + 0x10*MOD_SLOT, 0x32).items()))
    if ex: d.update(ex)
    return d

hoopscr('the size up, which will not fit',
        sewboth(sewpress(0x01, 0x07), _half(w16(0x0011A626, 0x0064))))
hoopscr('the size up, which fits',
        sewboth(sewpress(0x01, 0x07), _half()), small=True)
hoopscr('the size up at its ceiling',
        sewboth(sewpress(0x01, 0x07), b8(0x0011A25A + 0x10*MOD_SLOT, 0x64)))
hoopscr('the size up one below its ceiling',
        sewboth(sewpress(0x01, 0x07),
                _half(b8(0x0011A25A + 0x10*MOD_SLOT, 0x63))), small=True)
hoopscr('the size up with the second at its ceiling',
        sewboth(sewpress(0x01, 0x07),
                _half(b8(0x0011A25B + 0x10*MOD_SLOT, 0x64))))
# Exactly on the ceiling on one of the two and one below it on the other, in
# a hoop the bigger size still fits: the only shape in which widening either
# test by one shows.
hoopscr('the size up with the first exactly on its ceiling',
        sewboth(sewpress(0x01, 0x07),
                dict(list(b8(0x0011A25A + 0x10*MOD_SLOT, 0x64).items()) +
                     list(b8(0x0011A25B + 0x10*MOD_SLOT, 0x63).items()))),
        small=True)
hoopscr('the size up with the second exactly on its ceiling',
        sewboth(sewpress(0x01, 0x07),
                dict(list(b8(0x0011A25A + 0x10*MOD_SLOT, 0x63).items()) +
                     list(b8(0x0011A25B + 0x10*MOD_SLOT, 0x64).items()))),
        small=True)
# The two percentages kept apart, so which of them is put by in H'11F299 and
# which in H'11F29A is visible at all.
hoopscr('the size back to a hundred with the two apart',
        sewboth(sewpress(0x01, 0x06),
                dict(list(b8(0x0011A25A + 0x10*MOD_SLOT, 0x28).items()) +
                     list(b8(0x0011A25B + 0x10*MOD_SLOT, 0x3C).items()))),
        small=True)
hoopscr('the size up, a state bit up',
        sewboth(sewpress(0x01, 0x07), _half(b8(0x00114D55, 0x02))))
hoopscr('the size up, a busy link',
        sewboth(sewpress(0x01, 0x07), _half(b8(0x0011F29E, 0x01))))

# H'08 and H'19, the two ways out.
hoopscr('the first way out', sewpress(0x01, 0x08))
hoopscr('the first way out, something already asked for',
        sewboth(sewpress(0x01, 0x08), b8(0x00114D72, 0x01)))
hoopscr('the first way out, with a panel to put away',
        sewboth(sewpress(0x01, 0x08), b8(0x0011F4E6, 0x01)))
hoopscr('the first way out, the module going home',
        sewboth(sewpress(0x01, 0x08), b8(0x00114D66, 0x01)))
hoopscr('the second way out', sewpress(0x01, 0x19))
hoopscr('the second way out, something already asked for',
        sewboth(sewpress(0x01, 0x19), b8(0x00114D72, 0x01)))
hoopscr('the second way out, a busy link',
        sewboth(sewpress(0x01, 0x19), b8(0x0011F29E, 0x01)))
hoopscr('the second way out, with a panel to put away',
        sewboth(sewpress(0x01, 0x19), b8(0x0011F4E6, 0x01)))

# H'09, H'0A and H'0B, the three that talk to the module. The first two want
# the module running and the third wants it able to talk.
for _v, _nm in ((0x09, 'the first module key'), (0x0A, 'the second module key')):
    hoopscr(_nm, sewboth(sewpress(0x01, _v), b8(0x00FFFEC0, 0x04)))
    hoopscr('%s, the module not running' % _nm, sewpress(0x01, _v))
    hoopscr('%s, the module going home' % _nm,
            sewboth(sewpress(0x01, _v),
                    dict(list(b8(0x00FFFEC0, 0x04).items()) +
                         list(b8(0x00114D66, 0x01).items()))))
    hoopscr('%s, a busy link' % _nm,
            sewboth(sewpress(0x01, _v),
                    dict(list(b8(0x00FFFEC0, 0x04).items()) +
                         list(b8(0x0011F29E, 0x01).items()))))
# H'2431C2 parks the machine on its way out, and the park spins on a counter
# only an interrupt moves unless the needle is already at H'1B.
def _talks(ex=None):
    d = dict(list(b8(0x00FFFEC0, 0x04).items()) +
             list(b8(0x00FFFEC1, 0x00).items()) +
             list(b8(0x00FFFED3, 0x1B).items()))
    if ex: d.update(ex)
    return d

# H'2431C2 goes on into H'243E5C and the stitch machinery, which this fill
# does not set up -- the two images part company inside H'11A67D and the
# timers, not in anything this press does. Every case here therefore has the
# talk already ended, which is the branch that skips it; H'2431C2 has six
# cases of its own under the stitch fill.
hoopscr('the third module key',
        sewboth(sewpress(0x01, 0x0B), _talks(b8(0x00114DA0, 0x01))))
hoopscr('the third module key, the module not able to talk',
        sewpress(0x01, 0x0B))
hoopscr('the third module key, the module going home',
        sewboth(sewpress(0x01, 0x0B),
                dict(list(b8(0x00114DA0, 0x01).items()) +
                     list(_talks(b8(0x00114D66, 0x01)).items()))))
hoopscr('the third module key, a busy link',
        sewboth(sewpress(0x01, 0x0B),
                dict(list(b8(0x00114DA0, 0x01).items()) +
                     list(_talks(b8(0x0011F29E, 0x01)).items()))))

# H'0C, the redraw.
hoopscr('the redraw', sewpress(0x01, 0x0C))
hoopscr('the redraw, the module going home',
        sewboth(sewpress(0x01, 0x0C), b8(0x00114D66, 0x01)))
hoopscr('the redraw, the slot moved',
        sewboth(sewpress(0x01, 0x0C), b8(0x00114D73, 0x01)))
hoopscr('the redraw, a fault to report',
        sewboth(sewpress(0x01, 0x0C), b8(0x0011F534, 0x01)))

# H'0E to H'16, the nine keys that move the hoop. H'12 is the one in the
# middle, which does not count the step.
for _v in range(0x0E, 0x17):
    hoopscr('the hoop moved, key %02X' % _v,
            sewboth(sewpress(0x01, _v), b8(0x00FFFEC0, 0x04)))
hoopscr('the hoop moved with the second counter up',
        sewboth(sewpress(0x01, 0x0E),
                dict(list(b8(0x00FFFEC0, 0x04).items()) +
                     list(b8(0x00114DB1, 0x01).items()))))
hoopscr('the hoop put back with the second counter up',
        sewboth(sewpress(0x01, 0x12),
                dict(list(b8(0x00FFFEC0, 0x04).items()) +
                     list(b8(0x00114DB1, 0x01).items()))))
hoopscr('the hoop moved, the module not running', sewpress(0x01, 0x0E))
hoopscr('the hoop moved, the module going home',
        sewboth(sewpress(0x01, 0x0E),
                dict(list(b8(0x00FFFEC0, 0x04).items()) +
                     list(b8(0x00114D66, 0x01).items()))))
# The two bits H'11F2A2 carries are both nought in the fill, so neither the
# one the nudge sets nor the one it clears has anywhere to show unless the
# byte starts with both up.
hoopscr('the hoop moved with both of its bits already up',
        sewboth(sewpress(0x01, 0x0E),
                dict(list(b8(0x00FFFEC0, 0x04).items()) +
                     list(b8(0x0011F2A2, 0xFF).items()))))
hoopscr('the hoop put back with both of its bits already up',
        sewboth(sewpress(0x01, 0x12),
                dict(list(b8(0x00FFFEC0, 0x04).items()) +
                     list(b8(0x0011F2A2, 0xFF).items()))))
hoopscr('the hoop moved, a busy link',
        sewboth(sewpress(0x01, 0x0E),
                dict(list(b8(0x00FFFEC0, 0x04).items()) +
                     list(b8(0x0011F29E, 0x01).items()))))

# H'17 and H'18, the two that turn the design. H'114DB6 says the key is being
# held, which takes six steps at a time.
for _v, _nm in ((0x17, 'turned back'), (0x18, 'turned on')):
    hoopscr(_nm, sewpress(0x01, _v), small=True)
    hoopscr('%s, the key held' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x00114DB6, 0x01)), small=True)
    hoopscr('%s, in a hoop nothing turns in' % _nm, sewpress(0x01, _v))
    hoopscr('%s, the key held in a hoop nothing turns in' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x00114DB6, 0x01)))
    hoopscr('%s, a busy link' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x0011F29E, 0x01)), small=True)
    hoopscr('%s, a state bit up' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x00114D55, 0x02)), small=True)
    hoopscr('%s, something to report' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x00114D51, 0x40)), small=True)
    hoopscr('%s, a fault to report' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x0011F534, 0x01)), small=True)
    hoopscr('%s, a reset wanted' % _nm,
            sewboth(sewpress(0x01, _v), b8(0x00114D9F, 0x01)), small=True)
hoopscr('turned back off the bottom',
        sewboth(sewpress(0x01, 0x17), b8(0x0011A260 + 0x10*MOD_SLOT, 0x01)),
        small=True)
hoopscr('turned back off the bottom, the key held',
        sewboth(sewpress(0x01, 0x17),
                dict(list(b8(0x0011A260 + 0x10*MOD_SLOT, 0x03).items()) +
                     list(b8(0x00114DB6, 0x01).items()))), small=True)
hoopscr('turned on to the top',
        sewboth(sewpress(0x01, 0x18), b8(0x0011A260 + 0x10*MOD_SLOT, 0x47)),
        small=True)
hoopscr('turned on past the top, the key held',
        sewboth(sewpress(0x01, 0x18),
                dict(list(b8(0x0011A260 + 0x10*MOD_SLOT, 0x45).items()) +
                     list(b8(0x00114DB6, 0x01).items()))), small=True)
# A hoop the design only just turns in, where six steps do not fit and five
# do. That is the only shape in which the six the held key tries is visible
# as a number rather than as the step it then takes.
def _tight(rot, ex=None):
    d = collections.OrderedDict()
    d.update(w16(0x00104CCE + 2*MOD_DESIGN, 0x003C))
    d.update(w16(0x00104D06 + 2*MOD_DESIGN, 0x0050))
    d.update(w16(0x0011A626, 0x00C8))
    d.update(w16(0x0011A628, 0x00C8))
    d.update(b8(0x0011A260 + 0x10*MOD_SLOT, rot))
    d.update(b8(0x00114DB6, 0x01))
    if ex: d.update(ex)
    return d

hoopscr('turned back six in a hoop only five fit in',
        sewboth(sewpress(0x01, 0x17), _tight(0x08)))
hoopscr('turned on six in a hoop only five fit in',
        sewboth(sewpress(0x01, 0x18), _tight(0x0A)))
hoopscr('turned on to a label that needs the whole turn added',
        sewboth(sewpress(0x01, 0x18), b8(0x0011A260 + 0x10*MOD_SLOT, 0x25)),
        small=True)

# H'2257A6
for _nm, _arrived, _relayout, _ex in (
        ('just arrived', 1, 0, None),
        ('laid out again', 0, 1, None),
        ('a plain pass', 0, 0, None),
        ('a press on the first box', 0, 0,
         dict(list(b8(0x00FFFED9, 0x0C).items()) +
              list(b8(0x00FFFEDA, 0x24).items()) +
              list(boxval(1, 1).items())))):
    e = pat_extra(_ex)
    dispatch_case(_nm, 0x15, _arrived, _relayout, 0, extra=e)
    move_wipe(new[-1])
    new[-1]['steps'] = 60000000
    new[-1]['name'] = 'screen_body_15 (%s)' % _nm

# A last look over every case for the fill-ordering trap.
_seen = set()
for c in new:
    for k, w in audit(c):
        if (k, w) in _seen: continue
        _seen.add((k, w))
        print('  shadowed: %s by %s  (%s)' % (k, w, c['name']))

json.dump(new, open('/tmp/newcases.json','w'))
print(len(new))
