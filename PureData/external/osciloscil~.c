#include "m_pd.h"
#include "math.h"

/* Define constants and defaults */
#define OSCILOSCIL_DEFAULT_TABLESIZE 8192
#define OSCILOSCIL_DEFAULT_WAVEFORM "sine"
#define OSCILOSCIL_MAX_TABLESIZE 1048576

/* The class pointer */

static t_class *osciloscil_class;

/* The object structure */

typedef struct _osciloscil
{
	t_object obj; // the Pd object
	t_float x_f; // internally convert floats to signals
	long table_length; // length of wavetable
	float *wavetable; // wavetable
	t_symbol *waveform; // the waveform used currently
	float phase; // wavetable phase
	float si; // sampling increment
	float si_factor; // factor for generating the sampling increment
	float piotwo; // pi over two
	float twopi; // two pi
	float sr; // sampling rate

	long wavetable_bytes;// number of bytes stored in wavetable
	float *old_wavetable;// older wave table
	short dirty;// flag that wavetable is dirty
} t_osciloscil;

/* Function prototypes */

void *osciloscil_new(t_symbol *s, short argc, t_atom *argv);
t_int *osciloscil_perform(t_int *w);
void osciloscil_dsp(t_osciloscil *x, t_signal **sp, short *count);
void osciloscil_build_waveform( t_osciloscil *x );

//void osciloscil_assist(t_osciloscil *x, void *b, long msg, long arg, char *dst);

void osciloscil_sine(t_osciloscil *x);
void osciloscil_triangle(t_osciloscil *x);
void osciloscil_square(t_osciloscil *x);
void osciloscil_sawtooth(t_osciloscil *x);
void osciloscil_pulse(t_osciloscil *x);
void osciloscil_list (t_osciloscil *x, t_symbol *msg, short argc, t_atom *argv);
void osciloscil_free(t_osciloscil *x);

/* The object setup function */

void osciloscil_tilde_setup (void)
{
	t_class * c;
	osciloscil_class = class_new( gensym("osciloscil~"), (t_newmethod) osciloscil_new, (t_method)osciloscil_free, sizeof(t_osciloscil), 0, A_GIMME, 0);
	CLASS_MAINSIGNALIN(osciloscil_class, t_osciloscil, x_f);
	c = osciloscil_class;
	class_addmethod(c,(t_method)osciloscil_dsp, gensym("dsp"),0);

	class_addmethod(c,(t_method)osciloscil_sine, gensym("sine"), 0);
	class_addmethod(c,(t_method)osciloscil_triangle, gensym("triangle"), 0);
	class_addmethod(c,(t_method)osciloscil_square, gensym("square"), 0);
	class_addmethod(c,(t_method)osciloscil_sawtooth, gensym("sawtooth"), 0);
	class_addmethod(c,(t_method)osciloscil_pulse, gensym("pulse"), 0);
	class_addmethod(c,(t_method)osciloscil_list, gensym("list"), A_GIMME, 0);
}

/* The new instance routine */

void *osciloscil_new(t_symbol *s, short argc, t_atom *argv)
{
	float init_freq; 
	
	/* Instantiate the object */
	
	t_osciloscil *x = (t_osciloscil *) pd_new(osciloscil_class);

    inlet_new(&x->obj, &x->obj.ob_pd,gensym("signal"), gensym("signal"));
    inlet_new(&x->obj, &x->obj.ob_pd,gensym("signal"), gensym("signal"));
    
	/* Create one outlet */
	
	outlet_new(&x->obj, gensym("signal"));
		
	/* Initialize the object with default parameters */ 
	
	x->table_length = OSCILOSCIL_DEFAULT_TABLESIZE;
	x->waveform = gensym(OSCILOSCIL_DEFAULT_WAVEFORM);
	x->dirty = 0;
	
	/* Read any user-specified parameters */

	if(argc >= 1){ init_freq = atom_getfloatarg(0, argc, argv); }
	if(argc >= 2){ x->table_length = (long) atom_getintarg(1, argc, argv); }
	if(argc >= 3){ x->waveform = atom_getsymbolarg(2, argc, argv); }
	if(argc >= 4){ x->bl_harms = (long) atom_getintarg(3, argc, argv); }
	
	
	/* 
	 Check for illegal values supplied by the user, and replace all
	 illegal values with defaults. 
	 */
	
	if(fabs(init_freq) > 1000000){
		init_freq = OSCILOSCIL_DEFAULT_FREQUENCY;
		post("bad freq reset");
	}
	if(x->table_length < 4 || x->table_length > OSCILOSCIL_MAX_TABLESIZE){
		x->table_length = OSCILOSCIL_DEFAULT_TABLESIZE;
	}
	if(x->bl_harms < 1 || x->bl_harms > OSCILOSCIL_MAX_HARMS){
		post("bad harms reset from: %d", x->bl_harms);
		x->bl_harms = OSCILOSCIL_DEFAULT_HARMS;

	}
	
	/* Generate 2π and π/2 */
	
	x->twopi = 8.0 * atan(1.0);
	x->piotwo = 2. * atan(1.0);
	
	/* 
	 Allocate dynamic memory. Note that we do not check if the memory returned was valid. 
	 This code could be improved by checking that the returned memory pointer is not NULL.
	 */
	
	x->wavetable_bytes = x->table_length * sizeof(float);
	x->wavetable = (float *) getbytes(x->wavetable_bytes);
	x->old_wavetable = (float *) getbytes(x->wavetable_bytes);
	
	/*
	 Initialize the phase to zero. It is essential that we initialize the phase variable, 
	 otherwise it could contain some random value outside 0 - (table_length - 1), 
	 in which case the first lookup from the wavetable would crash the program.
	 */
	
	x->phase = 0;
	
	/* 
	 Programmatically determine the sampling rate, but note that we will need
	 to recheck this value in the DSP method. 
	 */
	
	x->sr = sys_getsr(); 
	
	/* Note that it is dangerous to divide by the x->sr below, which could in 
	 certain cases be zero. This code could be improved by first checking the
	 value of x->sr. 
	 */
	
	x->si_factor = (float) x->table_length / x->sr;
	x->si = init_freq * x->si_factor ;
	
	/* Branch to the appropriate method to initialize the waveform */
	
	if (x->waveform == gensym("sine")) {
		osciloscil_sine(x);
	} else if (x->waveform == gensym("triangle")) {
		osciloscil_triangle(x);
	} else if (x->waveform == gensym("square")) {
		osciloscil_square(x);
	} else if (x->waveform == gensym("sawtooth")) {
		osciloscil_sawtooth(x);
	} else if (x->waveform == gensym("pulse")) {
		osciloscil_pulse(x);
	} else {
		error("%s is not a legal waveform - using sine wave instead", x->waveform->s_name);
		osciloscil_sine(x);
	}
	
	/* Return a pointer to the object */
	
	return x;
}

/* The sine method */

void osciloscil_sine(t_osciloscil *x)
{
	
	osciloscil_build_waveform(x);
}

/* The triangle method */

void osciloscil_triangle(t_osciloscil *x)
{
	
	/* Build the waveform */
	
	osciloscil_build_waveform(x);
}

/* The sawtooth method */

void osciloscil_sawtooth(t_osciloscil *x)
{

	osciloscil_build_waveform(x);
}

/* The square method */

void osciloscil_square(t_osciloscil *x)
{
	osciloscil_build_waveform(x);
}


/* The pulse method */

void osciloscil_pulse(t_osciloscil *x)
{
	osciloscil_build_waveform(x);
}

/* The list method */

void osciloscil_list (t_osciloscil *x, t_symbol *msg, short argc, t_atom *argv)
{
	short i;
	int harmonic_count = 0;
	
	/* Read the list of harmonic weightings from the calling message */
	
	for (i=0; i < argc; i++) 
    {
        if (argv[i].a_type == A_FLOAT ){

            post("fa");
        }
        else
            post("yoze");
	}
	
	/* Set the harmonic count to the number of weightings read */
	
	osciloscil_build_waveform(x);
}

/* The utility function to build the waveform */

void osciloscil_build_waveform(t_osciloscil *x) {
	float rescale;
	int i, j;
	float max;
	float *wavetable = x->wavetable;
	
	/* Add 1 to the harmonic count to account for the DC component */
	
	int table_length = x->table_length;
	float twopi = x->twopi;
	float *old_wavetable = x->old_wavetable;
	
	/* Copy the current current wavetable to old wavetable */
	
	for(i = 0; i < table_length ; i++){
		old_wavetable[i] = wavetable[i];
	}
	
	/* 
	 Set the dirty flag, so that the osciloscillator will read from the old
	 wavetable until the new one has been generated.
	 */
	
	x->dirty = 1;
	
	/* Sum all specified harmonics into the wavetable */
	
	for(i = 1 ; i < partial_count; i++){
		if(amplitudes[i]){
			for(j = 0; j < table_length; j++){
				
				/* Notice that we are accumulating samples into the wavetable, thus the += operator */
				
				wavetable[j] += amplitudes[i] * sin(twopi * ((float)i * ((float)j/(float)table_length))) ;
			}
		}
	}
	
	
	/* Release the new wave table */
	
	x->dirty = 0; 
}


/* The perform routine */

t_int *osciloscil_perform(t_int *w)
{
	t_osciloscil *x = (t_osciloscil *) (w[1]);
	float *frequency = (t_float *)(w[2]);
	float *phases = (t_float *)(w[3]);
	float *waves = (t_float *)(w[4]);
	float *out = (t_float *)(w[5]);
	int n = w[6];
	
	float si_factor = x->si_factor;
	float si = x->si;
	float phase = x->phase;
	int table_length = x->table_length;
	float *wavetable = x->wavetable;
	float *old_wavetable = x->old_wavetable;
	float piotwo = x->piotwo;
	
	long iphase;
	float fraction;	
	
	while (n--) {
		
		/* Calculate the sampling increment */
		
		si = *frequency++ * si_factor;
		
		/* Truncate the local phase */
		
		iphase = floor(phase);
		
		/* 
		 In case the wavetable is being rebuilt,
		 read from the old copy of the wavetable.
		 */
		
		if(x->dirty){
			*out++ = old_wavetable[iphase];
		} 
		else {
			*out++ = wavetable[iphase];	
		}	
		
		/* Increment the phase */
		
		phase += si;
		
		/* Keep the phase within range of the table length*/
		
		while(phase >= table_length) {
			phase -= table_length;
		}
		while(phase < 0) {
			phase += table_length;
		}
	}
	
	/* Store the current phase */
	
	x->phase = phase;
	
	/* Return the next address on the DSP chain */
	
	return w + 7;
}

/* The free routine */

void osciloscil_free(t_osciloscil *x)
{
	t_freebytes(x->wavetable, x->wavetable_bytes);
	t_freebytes(x->old_wavetable, x->wavetable_bytes);
}

/* The DSP routine */

void osciloscil_dsp(t_osciloscil *x, t_signal **sp, short *count)
{
	
	if(x->sr != sp[0]->s_sr){
		if(! sp[0]->s_sr){
			error("zero sampling rate!");
			return;
		}
		x->si *= x->sr / sp[0]->s_sr;
		x->sr = sp[0]->s_sr;
		x->si_factor = (float) x->table_length / x->sr;
	}
	
	dsp_add(osciloscil_perform, 6, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[0]->s_n);
}
