#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#endif

/* Stuff for clock control. */

struct clock {
    unsigned int        run;            /* Run/stop. */
    unsigned int        go;             /* Start burst. */
    unsigned int        fast;           /* Start burst. */
    unsigned int        cycles_slow;    /* Cycles/burst - slow. */
    unsigned int        cycles_fast;    /* Cycles/burst - fast. */
    unsigned int        rate;           /* Clock rate: unit is 0.1 Hz. */
    GtkToggleButton    *run_button;
    GtkSpinButton      *burst;
};

/* Boundary between animated and full-speed running. */

#define MAX_ANIMATED_RATE 100           /* 10Hz clock. */

/* Stuff for a row of lights. */

typedef GtkWidget *Button;

/* This structure describes a displayed register. */

typedef enum update_state {
    Valid = 0, User, Simulation
} Update_state;

struct reg {
    unsigned int        width;          /* Number of bits. */
    unsigned int        value;          /* Register contents. */
    unsigned int        flags;          /* Per-bit settings. */
    unsigned int        options;        /* Bitfield, see sim.h. */
    Update_state        state;          /* Update pending? */
    struct reg         *clones;         /* Others with same handle. */
    struct reg         *chain;          /* Pending update list. */
    Sim_RH              handle;         /* Simulator's handle. */
    char               *name;
    union {
        struct {                        /* Display individual bits. */
            unsigned int        prev_value, prev_flags;
            Button              buttons[];
        }                   b;
        struct {                        /* Text entry widget. */
            unsigned int         max_len;
            GtkWidget           *entry;
        }                   e;
    }                   u;
};
        

/* List of struct_regs with pending simulator updates - mutex locked. */

extern struct reg *User_modified_regs;

#define EXIT_VALUE ((struct reg *)1) // Magic value.

/* Structure passed from simulator to describe a row of registers. */

struct row {
    char               *name;
    int                 count;          /* How many regs? */
    struct reg         *regs[];
};

/* Structure passed from simulator to describe an overlayed display. */

#define MAX_OVERLAY_ITEMS  4

struct overlayed_regs {
    char               *name;
    unsigned int        choice;         /* Which one to show? */
    int                 count;          /* How many regs? */
    GtkStack           *stack;          /* The display area. */
    GtkWidget          *items[MAX_OVERLAY_ITEMS];
};

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

extern void Start_Panel(const char * title);

/* Functions called via the Glib loop idle mechanism - cross thread calls. */

/* Set a register value or display flags, from the simulation thread. */

gboolean Simulation_call(gpointer data);  /* Arg is struct reg *. */
gboolean Flag_call(gpointer data);        /* Arg is struct reg *. */

/* Change the visible component in an overlay. */

gboolean Overlay_switch(gpointer data); /* Arg is struct overlayed_regs *. */

/* Create new visible items. */

gboolean New_reg_call(gpointer data);     /* Argument is struct reg *. */
gboolean New_row_call(gpointer data);     /* Argument is struct row *. */
gboolean Overlay_call(gpointer data);     /* Argument is struct overlay. */
