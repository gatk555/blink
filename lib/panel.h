#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#endif

/* Stuff for a row of lights. */

typedef GtkWidget *Button;

/* This structure describes a displayed register. If modifying, check
 * the definition of REGISTER_BASE_SIZE below.
 */

typedef enum update_state {
    Valid = 0, User, Simulation
} Update_state;

struct reg {
    char               *name;
    unsigned int        width;          /* Number of bits. */
    union {
        struct {
            unsigned int        value;  /* Register contents. */
            unsigned int        flags;  /* Per-bit settings. */
        }                   u;
        double              fp_value;   /* It shows floating-point. */
    }                   v;
    unsigned int        options;        /* Bitfield, see sim.h. */
    Update_state        state;          /* Update pending? */
    struct reg         *clones;         /* Others with same handle. */
    struct reg         *chain;          /* Pending update list. */
    Sim_RH              handle;         /* Simulator's handle. */
    union {
        struct {                        /* Display individual bits. */
            unsigned int        prev_value, prev_flags;
            Button              buttons[];
        }                   b;
        struct {                        /* Text entry or combo-box widget. */
            unsigned int         max_len;
            GtkWidget           *entry;
            const char          * const *strings; // For combo-box.
        }                   e;
    }                   u;
};

#define u_value v.u.value
#define u_flags v.u.flags
#define fp_value v.fp_value

#define u_entry u.e.entry
#define u_max_len u.e.max_len

/* List of struct_regs with pending simulator updates - mutex locked. */

extern struct reg *User_modified_regs;

#define EXIT_VALUE ((struct reg *)1) // Magic value.

/* Stuff for clock control. */

struct clock {
    unsigned int        sim_ctl;        /* Simulator controlling execution. */
    unsigned int        run;            /* Run/stop. */
    unsigned int        go;             /* Start burst. */
    unsigned int        fast;           /* Start burst. */
    unsigned int        cycles_slow;    /* Cycles/burst - slow. */
    unsigned int        cycles_fast;    /* Cycles/burst - fast. */
    unsigned int        cycles_sim;     /* Cycles/burst - simulator's own. */
    unsigned int        rate;           /* Clock rate: unit is 0.1 Hz. */
    unsigned int        unit;           /* Passed to simulator. */
    GtkToggleButton    *run_button;
    GtkComboBox        *combo;
    GtkSpinButton      *burst;
    struct reg          unit_reg;       /* Dummy registers for combo-box. */
};

#define COMBO_HANDLE ((Sim_RH)&The_clock.unit_reg) // Dummy handle

/* Boundary between animated and full-speed running. */

#define MAX_ANIMATED_RATE 100           /* 10Hz clock. */

/* Structure passed from simulator to describe a row of registers. */

struct row {
    char               *name;
    int                 count;          /* How many regs? */
    struct thing       *items[MAX_ITEMS];
};

/* Structure passed from simulator to describe a grid of registers. */

struct grid {
    char               *name;
    int                 columns;
    int                 count;          /* How many regs? */
    struct thing       *items[MAX_ITEMS];
};

/* Structure passed from simulator to describe an overlayed display. */

#define MAX_OVERLAY_ITEMS  4

struct overlay {
    char               *name;
    unsigned int        choice;         /* Which one to show? */
    int                 count;          /* How many regs? */
    GtkStack           *stack;          /* The display area. */
    GtkWidget          *items[MAX_ITEMS];
};

/* The simulator-side handles resolve to pointers to this stucture. */

struct thing {
    enum {Register, Row, Grid, Overlay} type;
    union {
        struct reg     reg;
        struct row     row;
        struct grid    grid;
        struct overlay overlay;}  u;
};

/* Sizes of various "things" without any trailing variable-length arrays. */

#define BASE_SIZE(u_member, s_member) \
    ((uintptr_t)&((struct thing *)0)->u.u_member.s_member)
#define REGISTER_BASE_SIZE (BASE_SIZE(reg, u.e.strings) + sizeof (char *))
#define ROW_BASE_SIZE BASE_SIZE(row, items[MAX_ITEMS + 1])
#define GRID_BASE_SIZE BASE_SIZE(grid, items[MAX_ITEMS + 1])
#define OVERLAY_BASE_SIZE BASE_SIZE(overlay, items[MAX_ITEMS + 1])

/* Global data and functions. */

extern struct clock           The_clock;

extern GdkPixbuf *Lamps[4];     /* Images for button state indicators. */

#define BLACK 0
#define RED   1
#define BLUE  2
#define GREEN 3

extern void Init_lights(void);

/* Locking for the above. */

extern GMutex           Simulation_mutex;

/* To wake sleeping simulation thread. */

extern GCond            Simulation_waker;

/* Functions. */

extern void Start_Panel(const char * title,
                        const char **unit_strings, unsigned int initial_unit);

/* Functions called via the Glib loop idle mechanism - cross thread calls. */

/* Create new visible items. */

gboolean New_thing_call(gpointer data);   /* Argument is struct thing *. */

/* Set a register value or display flags, from the simulation thread. */

gboolean Simulation_call(gpointer data);  /* Arg is struct reg *. */
gboolean Flag_call(gpointer data);        /* Arg is struct reg *. */

/* Update the "burst" field. */

gboolean Display_burst(gpointer data);

/* Change the visible component in an overlay. */

gboolean Overlay_switch(gpointer data); /* Arg is struct overlayed_regs *. */

/* Set combo-box strings. */

gboolean New_strings_call(gpointer data); /* Argument is struct reg *. */

