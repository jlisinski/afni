#include "afni.h"
#include "mri_render.h"
#include "mcw_graf.h"
#include "parser.h"

#ifndef ALLOW_PLUGINS
#  error "Plugins not properly set up -- see machdep.h"
#endif

#undef REND_DEBUG

/***********************************************************************
  Plugin to render a volume dataset.  Makes a custom interface.
  Modified from plug_drawdset.c by RWCox - February 1999.
************************************************************************/

/*---------- prototypes for internal routines ----------*/

char * REND_main( PLUGIN_interface * ) ;                 /* called from AFNI */

void REND_make_widgets(void) ;                           /* initialization */

void REND_done_CB   ( Widget , XtPointer , XtPointer ) ; /* Done button */
void REND_draw_CB   ( Widget , XtPointer , XtPointer ) ; /* Draw button */
void REND_help_CB   ( Widget , XtPointer , XtPointer ) ; /* Help button */
void REND_reload_CB ( Widget , XtPointer , XtPointer ) ; /* Reload button */
void REND_choose_CB ( Widget , XtPointer , XtPointer ) ; /* Choose button */
void REND_xhair_CB  ( Widget , XtPointer , XtPointer ) ; /* See Xhairs toggle */
void REND_dynamic_CB( Widget , XtPointer , XtPointer ) ; /* DynaDraw toggle */
void REND_accum_CB  ( Widget , XtPointer , XtPointer ) ; /* Accumulate toggle */
void REND_angle_CB  ( MCW_arrowval * , XtPointer ) ;     /* Angle arrowvals */
void REND_param_CB  ( MCW_arrowval * , XtPointer ) ;     /* Cutout arrowvals */
void REND_precalc_CB( MCW_arrowval * , XtPointer ) ;     /* Precalc menu */
void REND_clip_CB   ( MCW_arrowval * , XtPointer ) ;     /* Clip arrowvals */

void   REND_choose_av_CB      ( MCW_arrowval * , XtPointer ) ; /* Sub-brick menus */
char * REND_choose_av_label_CB( MCW_arrowval * , XtPointer ) ;
void   REND_opacity_scale_CB  ( MCW_arrowval * , XtPointer ) ;

void REND_finalize_dset_CB( Widget , XtPointer , MCW_choose_cbs * ) ; /* dataset chosen */

void REND_reload_dataset(void) ;                         /* actual reloading work */
void REND_xhair_overlay(void)  ;                         /* make the crosshairs */

static PLUGIN_interface * plint = NULL ;                 /* what AFNI sees */

void REND_textact_CB( Widget , XtPointer , XtPointer ) ; /* press Enter in a textfield */

static float angle_fstep  = 5.0 ;
static float cutout_fstep = 5.0 ;

/***********************************************************************
   Set up the interface to the user.  Note that we bypass the
   normal interface creation, and simply have the menu selection
   directly call the main function, which will create a custom
   set of interface widgets.
************************************************************************/

PLUGIN_interface * PLUGIN_init( int ncall )
{

   if( ncall > 0 ) return NULL ;  /* only one interface */

   plint = PLUTO_new_interface( "Render Dataset" , NULL , NULL ,
                                PLUGIN_CALL_IMMEDIATELY , REND_main ) ;

   PLUTO_add_hint( plint , "Volume Rendering" ) ;

   return plint ;
}

/***************************************************************************
                          Internal data structures
****************************************************************************/

#define NO_DATASET_STRING "[No Dataset is Loaded]"

/* Interface widgets */

static Widget shell=NULL , anat_rowcol , info_lab , choose_pb ;
static Widget done_pb , help_pb , draw_pb , reload_pb ;
static MCW_arrowval * roll_av , * pitch_av , * yaw_av , * precalc_av ;
static MCW_bbox * xhair_bbox , * dynamic_bbox , * accum_bbox ;
static MCW_arrowval * choose_av , * opacity_scale_av ;

static char * REND_dummy_av_label[2] = { "[Nothing At All]" , "[Nothing At All]" } ;

static Widget top_rowcol , anat_frame ;

#define CLIP_RANGE 32767

static Widget range_lab ;
static MCW_arrowval * clipbot_av , * cliptop_av ;

static float  brickfac = 0.0 ;
static Widget range_faclab , clipbot_faclab , cliptop_faclab ;

void REND_graf_CB( MCW_graf * , void * ) ;
static MCW_graf    * opa_graf ;
static MCW_graf    * gry_graf ;
static MCW_pasgraf * his_graf ;

static char * xhair_bbox_label[1]   = { "See Xhairs" } ;
static char * dynamic_bbox_label[1] = { "DynaDraw"   } ;
static char * accum_bbox_label[1]   = { "Accumulate" } ;

/* Other data */

#define MODE_LOW       0
#define MODE_MEDIUM    1
#define MODE_HIGH      2

#define NUM_precalc    3

static char * precalc_strings[] = { "Low" , "Medium" , "High" } ;
static int    precalc_mode[]    = { PMODE_LOW,PMODE_MEDIUM,PMODE_HIGH } ;

static MCW_DC * dc ;                   /* display context */
static Three_D_View * im3d ;           /* AFNI controller */
static THD_3dim_dataset * dset ;       /* The dataset!    */
static int new_dset = 0 ;              /* Is it new?      */
static int dset_ival = 0 ;             /* Sub-brick index */
static char dset_title[THD_MAX_NAME] ; /* Title string */

static MRI_IMAGE * grim , * opim ;  /* volumes to render */
static void * render_handle ;       /* rendering struct */

#define FREE_VOLUMES                                   \
  do{ if(grim != NULL){ mri_free(grim); grim = NULL; } \
      if(opim != NULL){ mri_free(opim); opim = NULL; } } while(0)

#define NEED_VOLUMES (grim == NULL || opim == NULL)

#ifdef SGI
static int   precalc_ival   = MODE_LOW    ;
#else
static int   precalc_ival   = MODE_MEDIUM ;
#endif

static int   dynamic_flag   = 0      ;
static int   accum_flag     = 0      ;
static float angle_roll     =   70.0 ;
static float angle_pitch    =  120.0 ;
static float angle_yaw      =    0.0 ;

static int xhair_flag  = 0    ;
static int xhair_ixold = -666 ;  /* remember the past */
static int xhair_jyold = -666 ;
static int xhair_kzold = -666 ;

#define CHECK_XHAIR_MOTION ( im3d->vinfo->i1 != xhair_ixold || \
                             im3d->vinfo->j2 != xhair_jyold || \
                             im3d->vinfo->k3 != xhair_kzold   )

static int   post_facto_load = 0 ;  /* in case the user loads before Draw */

static int renderer_open   = 0 ;

static int npixels = 0 ;

/*------------------  Stuff for the image display window -----------------*/

static MCW_imseq * imseq      = NULL ;
static MRI_IMARR * renderings = NULL ;

void REND_open_imseq( void ) ;
void REND_update_imseq( void ) ;
void REND_destroy_imseq( void ) ;
XtPointer REND_imseq_getim( int , int , XtPointer ) ;
void REND_seq_send_CB( MCW_imseq * , XtPointer , ISQ_cbs * ) ;

/*---------------- Stuff for Automate mode --------------*/

static MCW_bbox * automate_bbox ;

static int automate_flag = 0 ;
static char * automate_bbox_label[1]   = { "Automate" } ;

static MCW_arrowval * autoframe_av ;
static Widget autocompute_pb , autocancel_pb ;

void REND_autoflag_CB   (Widget , XtPointer , XtPointer) ; /* Automate toggle */
void REND_autocompute_CB(Widget , XtPointer , XtPointer) ; /* Compute pushbutton */
void REND_autocancel_CB (Widget , XtPointer , XtPointer) ; /* Cancel pushbutton */

/*-------------------------- Stuff for cutout logic ----------------------*/

void REND_cutout_type_CB( MCW_arrowval * , XtPointer ) ;
void REND_numcutout_CB  ( MCW_arrowval * , XtPointer ) ;
void REND_cutout_set_CB ( Widget , XtPointer , XtPointer ) ;

typedef struct {                            /* widgets for a cutout */
   Widget hrc , param_lab , set_pb ;
   MCW_arrowval * type_av , * param_av ;
   MCW_bbox * mustdo_bbox ;
} REND_cutout ;

REND_cutout * REND_make_cutout( int n ) ;   /* makes the widgets */

#define MAX_CUTOUTS 9
static REND_cutout * cutouts[MAX_CUTOUTS] ;

#define CUTOUT_OR  0
#define CUTOUT_AND 1
static char * cutout_logic_labels[] = { "OR" , "AND" } ;

static int num_cutouts  = 0 ;
static int logic_cutout = CUTOUT_OR ;

MCW_arrowval * numcutout_av ;
MCW_arrowval * logiccutout_av ;

typedef struct {                                     /* store the status */
   int num , logic ;                                 /* of the cutouts   */
   int   type[MAX_CUTOUTS] , mustdo[MAX_CUTOUTS] ;
   float param[MAX_CUTOUTS] , opacity_scale ;
   char  param_str[MAX_CUTOUTS][AV_MAXLEN+4] ;
} CUTOUT_state ;

#define MIN_OPACITY_SCALE 0.004

CUTOUT_state current_cutout_state , old_cutout_state ;

void REND_load_cutout_state(void) ;                  /* load from widgets */
int REND_cutout_state_changed(void) ;                /* has it changed? */
void REND_cutout_blobs(void) ;                       /* actually do the cutouts */

static char * mustdo_bbox_label[1] = { "Must Do" } ;

/*-----------------   stuff for evaluation of expressions   ------------*/

static double atoz[26] ;  /* values of 'a', 'b', ..., 'z' in expressions */

#define N_IND  13  /* 'n' */
#define T_IND  19  /* 't' */
#define X_IND  23  /* 'x' */
#define Y_IND  24  /* 'y' */
#define Z_IND  25  /* 'z' */

float REND_evaluate( MCW_arrowval * ) ;

/*-------------------- Functional overlay stuff ----------------------*/

#define INVALIDATE_OVERLAY \
  do{ if( ovim != NULL ){ mri_free(ovim); ovim = NULL; }} while(0)

#define DO_OVERLAY   (func_dset != NULL && func_see_overlay)
#define NEED_OVERLAY (DO_OVERLAY && ovim == NULL)
#define NEED_RELOAD  (NEED_VOLUMES || NEED_OVERLAY)

#define TURNOFF_OVERLAY_WIDGETS                                   \
  do{ XmString xstr ;                                              \
      xstr = XmStringCreateLtoR( NO_DATASET_STRING ,                \
                                 XmFONTLIST_DEFAULT_TAG ) ;          \
      XtVaSetValues( wfunc_info_lab , XmNlabelString,xstr , NULL ) ;  \
      XmStringFree(xstr) ;                                             \
                                                                        \
      xstr = REND_range_label() ;                                        \
      XtVaSetValues( wfunc_range_label , XmNlabelString , xstr , NULL ) ; \
      XmStringFree(xstr) ;                                                 \
                                                                            \
      xstr = REND_autorange_label() ;                                        \
      XtVaSetValues( wfunc_range_bbox->wbut[0], XmNlabelString,xstr, NULL ) ; \
      XmStringFree(xstr) ;                                                    \
                                                                             \
      AV_SENSITIZE( wfunc_color_av  , False ) ;                             \
      AV_SENSITIZE( wfunc_thresh_av , False ) ;                            \
  } while(0)

void REND_func_widgets(void) ;
void REND_init_cmap(void) ;
void REND_reload_func_dset(void) ;
void REND_reload_renderer(void) ;

static Widget wfunc_open_pb ;
void REND_open_func_CB( Widget , XtPointer , XtPointer ) ;

static Widget wfunc_frame=NULL , wfunc_rowcol , wfunc_choose_pb ,
              wfunc_uber_rowcol , wfunc_info_lab , wfunc_vsep ;

static Widget wfunc_thr_rowcol , wfunc_thr_label , wfunc_thr_scale=NULL ,
              wfunc_thr_pval_label ;
static MCW_arrowval * wfunc_thr_top_av ;

static Widget wfunc_color_rowcol , wfunc_color_label ;
static MCW_pbar * wfunc_color_pbar ;
static MCW_arrowval * wfunc_color_av , * wfunc_thresh_av , * wfunc_colornum_av ;
static MCW_bbox * wfunc_color_bbox ;

static Widget wfunc_choices_rowcol , wfunc_choices_label ,
              wfunc_buck_frame , wfunc_buck_rowcol ,
              wfunc_opacity_frame , wfunc_opacity_rowcol ,
              wfunc_range_rowcol , wfunc_range_frame ;

static Widget wfunc_range_label ;
static MCW_arrowval * wfunc_opacity_av , * wfunc_range_av ;
static MCW_bbox * wfunc_see_overlay_bbox , * wfunc_cut_overlay_bbox ,
                * wfunc_kill_isolas_bbox , * wfunc_range_bbox ;

static Widget wfunc_pbar_menu , wfunc_pbar_equalize_pb , wfunc_pbar_settop_pb ;
static MCW_arrowval * wfunc_pbar_palette_av ;

extern void REND_pbarmenu_CB( Widget , XtPointer , XtPointer ) ;
extern void REND_pbarmenu_EV( Widget , XtPointer , XEvent * , Boolean * ) ;
extern void REND_palette_av_CB( MCW_arrowval * , XtPointer ) ;
extern void REND_set_pbar_top_CB( Widget , XtPointer , MCW_choose_cbs * ) ;

#define DEFAULT_FUNC_RANGE 10000.0

static int   func_use_autorange = 1   ;
static float func_threshold     = 0.5 ;
static float func_thresh_top    = 1.0 ;
static int   func_use_thresh    = 1   ;
static float func_color_opacity = 0.5 ;
static int   func_see_overlay   = 0   ;
static int   func_cut_overlay   = 0   ;
static int   func_kill_isolas   = 0   ;
static int   func_posfunc       = 0   ;
static float func_range         = DEFAULT_FUNC_RANGE ;
static float func_autorange     = DEFAULT_FUNC_RANGE ;
static int   func_computed      = 0 ;

static THD_3dim_dataset * func_dset = NULL ;
static int func_color_ival  = 0 ;
static int func_thresh_ival = 0 ;

static byte func_rmap[256] , func_gmap[256] , func_bmap[256] ;
static int func_ncmap ;
static int func_cmap_set = 0 ;

static MRI_IMAGE * ovim ;

static char func_dset_title[THD_MAX_NAME] ; /* Title string */

char * REND_thresh_tlabel_CB( MCW_arrowval * , XtPointer ) ;
void REND_setup_color_pbar(void) ;
XmString REND_range_label(void) ;
XmString REND_autorange_label(void) ;

void REND_range_bbox_CB    ( Widget , XtPointer , XtPointer ) ;
void REND_color_bbox_CB    ( Widget , XtPointer , XtPointer ) ;
void REND_thr_scale_CB     ( Widget , XtPointer , XtPointer ) ;
void REND_thr_scale_drag_CB( Widget , XtPointer , XtPointer ) ;
void REND_see_overlay_CB   ( Widget , XtPointer , XtPointer ) ;
void REND_cut_overlay_CB   ( Widget , XtPointer , XtPointer ) ;
void REND_kill_isolas_CB   ( Widget , XtPointer , XtPointer ) ;
void REND_finalize_func_CB ( Widget , XtPointer , MCW_choose_cbs * ) ;

void REND_range_av_CB     ( MCW_arrowval * , XtPointer ) ;
void REND_thresh_top_CB   ( MCW_arrowval * , XtPointer ) ;
void REND_colornum_av_CB  ( MCW_arrowval * , XtPointer ) ;
void REND_color_opacity_CB( MCW_arrowval * , XtPointer ) ;

void REND_color_pbar_CB( MCW_pbar * , XtPointer , int ) ;
void REND_set_thr_pval(void) ;

#define COLSIZE 20

#undef FIX_SCALE_SIZE
#undef HIDE_SCALE
#ifdef FIX_SCALE_SIZE_PROBLEM
#  define FIX_SCALE_SIZE                                        \
     do{ int sel_height ;  XtPointer sel_ptr ;                  \
         if( wfunc_thr_scale != NULL ){                         \
           XtVaGetValues( wfunc_thr_scale ,                     \
                             XmNuserData , &sel_ptr , NULL ) ;  \
           sel_height = (int) sel_ptr ;                         \
           XtVaSetValues( wfunc_thr_scale ,                     \
                             XmNheight , sel_height , NULL ) ;  \
           XtManageChild(wfunc_thr_scale) ;                     \
       } } while(0)
#  define HIDE_SCALE \
     do{ if(wfunc_thr_scale != NULL) XtUnmanageChild(wfunc_thr_scale); } while(0)
#else
#  define FIX_SCALE_SIZE /* nada */
#  define HIDE_SCALE     /* nada */
#endif

/***************************************************************************
  Will be called from AFNI when user selects from Plugins menu.
****************************************************************************/

char * REND_main( PLUGIN_interface * plint )
{
   XmString xstr ;

   /*-- sanity checks --*/

   if( ! IM3D_OPEN(plint->im3d) ) return "AFNI Controller\nnot opened?!" ;

   if( renderer_open ){
      XtMapWidget(shell) ;
      XRaiseWindow( XtDisplay(shell) , XtWindow(shell) ) ;
      return ;
   }

   im3d = plint->im3d ;  /* save for local use */

   /*-- create widgets, first time through --*/

   if( shell == NULL ){
      dc = im3d->dc ;        /* save this too */
      REND_make_widgets() ;
   }

   /*-- set titlebar --*/

   { static char clabel[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" ; /* see afni_func.c */
     char ttl[PLUGIN_STRING_SIZE] ; int ic ;

     ic = AFNI_controller_index(im3d) ;  /* find out which controller */

     if( ic >=0 && ic < 26 ){
        sprintf( ttl , "AFNI Renderer [%c]" , clabel[ic] ) ;
        XtVaSetValues( shell , XmNtitle , ttl , NULL ) ;
     }
   }

   /*-- set some widget values --*/

   xstr = XmStringCreateLtoR( NO_DATASET_STRING ,
                              XmFONTLIST_DEFAULT_TAG ) ;
   XtVaSetValues( info_lab , XmNlabelString , xstr , NULL ) ;
   XmStringFree(xstr) ;

   xstr = XmStringCreateLtoR( "Min=?????? Max=??????" ,
                              XmFONTLIST_DEFAULT_TAG            ) ;
   XtVaSetValues( range_lab , XmNlabelString , xstr , NULL ) ;
   XmStringFree(xstr) ;

   AV_assign_ival( clipbot_av , -CLIP_RANGE ) ;
   AV_assign_ival( cliptop_av ,  CLIP_RANGE ) ;

   brickfac = 0.0 ;
   XtUnmanageChild( range_faclab   ) ;
   XtUnmanageChild( clipbot_faclab ) ;
   XtUnmanageChild( cliptop_faclab ) ;

   MCW_set_bbox( xhair_bbox   , 0 ) ; xhair_flag   = 0 ;
   MCW_set_bbox( dynamic_bbox , 0 ) ; dynamic_flag = 0 ;
   MCW_set_bbox( accum_bbox   , 0 ) ; accum_flag   = 0 ;

   MCW_set_bbox( automate_bbox , 0 ) ; automate_flag = 0 ;
   XtSetSensitive( autocompute_pb , False ) ;

   AV_assign_ival( numcutout_av , 0 ) ;      /* turn off cutouts */
   REND_numcutout_CB( numcutout_av , NULL ) ;

   REND_load_cutout_state() ; old_cutout_state = current_cutout_state ;

   AV_SENSITIZE( choose_av , False ) ;

   /*--- some of the function widgets, too ---*/

   if( wfunc_frame != NULL ){

      TURNOFF_OVERLAY_WIDGETS ;

   }

   /*-- pop the widget up --*/

   XtMapWidget(shell) ;
   PLUTO_cursorize(shell) ;

   /*-- misc initialization --*/

   dset          = NULL ;   /* not rendering anything     */
   dset_ival     = 0 ;      /* if we were, it would be #0 */
   renderer_open = 1 ;      /* renderer is now open for business */
   imseq         = NULL ;   /* no image window is open yet */
   grim = opim   = NULL ;   /* don't have volumes to render yet */
   render_handle = NULL ;   /* don't have a renderer yet */

   ovim          = NULL ;   /* no overlay volume yet */
   func_dset     = NULL ;   /* no functional dataset yet */

   post_facto_load = 0 ;    /* not needed yet */

   set_MCW_pasgraf( his_graf , NULL ) ;  /* set histogram graph to 0's */
   redraw_MCW_pasgraf( his_graf ) ;

   xhair_ixold = -666 ; xhair_jyold = -666 ; xhair_kzold = -666 ;

   return NULL ;
}

/*------------------------------------------------------------------------
  Make the control popup for this thing
--------------------------------------------------------------------------*/

/*-- structures defining action buttons (at bottom of popup) --*/

#define NACT 4  /* number of action buttons */

static MCW_action_item REND_actor[NACT] = {

 {"Help",REND_help_CB,NULL,
  "Displays more help" , "Displays more help",0} ,

 {"Draw",REND_draw_CB,NULL,
  "(Re)Draw the image" , "(Re)Draw the image",0} ,

 {"Reload",REND_reload_CB,NULL,
  "Reload dataset values" , "Reload dataset values",0} ,

 {"done",REND_done_CB,NULL,
  "Close renderer\nand image." , "Close windows",1}
} ;

#define SEP_HOR(ww)  XtVaCreateManagedWidget(                     \
                       "AFNI" , xmSeparatorWidgetClass , (ww) ,   \
                          XmNseparatorType , XmSINGLE_LINE ,      \
                          XmNinitialResourcesPersistent , False , \
                       NULL )

#define SEP_VER(ww) XtVaCreateManagedWidget(                      \
                       "AFNI" , xmSeparatorWidgetClass , (ww) ,   \
                          XmNseparatorType , XmDOUBLE_LINE ,      \
                          XmNorientation   , XmVERTICAL ,         \
                          XmNinitialResourcesPersistent , False , \
                       NULL )

void REND_make_widgets(void)
{
   XmString xstr ;
   char      str[64] ;
   Widget hrc , vrc ;
   int ii ;
   char * env ;
   float val ;

   /***----- retrieve relevant environment variables, if any -----***/

   env = getenv("AFNI_RENDER_ANGLE_DELTA") ;
   if( env != NULL ){
      val = strtod(env,NULL) ;
      if( val > 0.0 && val < 100.0 ) angle_fstep = val ;
   }

   env = getenv("AFNI_RENDER_CUTOUT_DELTA") ;
   if( env != NULL ){
      val = strtod(env,NULL) ;
      if( val > 0.0 && val < 100.0 ) cutout_fstep = val ;
   }

   env = getenv("AFNI_RENDER_PRECALC_MODE") ;
   if( env != NULL ){
      for( ii=0 ; ii < NUM_precalc ; ii++ )
         if( strcmp(env,precalc_strings[ii]) == 0 ) break ;
      if( ii < NUM_precalc ) precalc_ival = precalc_mode[ii] ;
   }

   /***=============================================================*/

   /*** top level shell for window manager ***/

   shell =
      XtVaAppCreateShell(
           "AFNI" , "AFNI" , topLevelShellWidgetClass , dc->display ,

           XmNtitle             , "AFNI Renderer" , /* top of window */
           XmNiconName          , "Renderer"      , /* label on icon */
           XmNdeleteResponse    , XmDO_NOTHING  ,   /* deletion handled below */
           XmNallowShellResize  , True ,            /* let code resize shell? */
           XmNmappedWhenManaged , False ,           /* must map it manually */
           XmNinitialResourcesPersistent , False ,
      NULL ) ;

   DC_yokify( shell , dc ) ; /* 14 Sep 1998 */

#ifndef DONT_INSTALL_ICONS
   if( afni48_good )             /* set icon pixmap */
      XtVaSetValues( shell ,
                        XmNiconPixmap , afni48_pixmap ,
                     NULL ) ;
#endif

   if( MCW_isitmwm(shell) )      /* remove some MWM functions */
      XtVaSetValues( shell ,
                       XmNmwmFunctions ,
                       MWM_FUNC_MOVE | MWM_FUNC_CLOSE | MWM_FUNC_MINIMIZE ,
                     NULL ) ;

   XmAddWMProtocolCallback(      /* make "Close" window menu work */
           shell ,
           XmInternAtom( dc->display , "WM_DELETE_WINDOW" , False ) ,
           REND_done_CB , (XtPointer) plint ) ;

   /*** horizontal rowcol to hold ALL interface stuff ***/

   top_rowcol =  XtVaCreateWidget(
                  "AFNI" , xmRowColumnWidgetClass , shell ,
                     XmNorientation  , XmHORIZONTAL ,
                     XmNpacking      , XmPACK_TIGHT ,
                     XmNadjustLast   , False ,
                     XmNadjustMargin , False ,
                     XmNtraversalOn  , False ,
                     XmNmarginWidth  , 0 ,
                     XmNmarginHeight , 0 ,
                     XmNinitialResourcesPersistent , False ,
                  NULL ) ;

   /*** vertical rowcolumn widget to hold anat interface stuff ***/

   anat_frame = XtVaCreateWidget(
                   "AFNI" , xmFrameWidgetClass , top_rowcol ,
                      XmNshadowType , XmSHADOW_ETCHED_IN ,
                      XmNshadowThickness , 5 ,
                      XmNtraversalOn , False ,
                      XmNinitialResourcesPersistent , False ,
                   NULL ) ;

   anat_rowcol = XtVaCreateWidget(
                  "AFNI" , xmRowColumnWidgetClass , anat_frame ,
                     XmNpacking     , XmPACK_TIGHT ,
                     XmNorientation , XmVERTICAL ,
                     XmNadjustLast  , False ,
                     XmNadjustMargin, False ,
                     XmNtraversalOn , False ,
                     XmNinitialResourcesPersistent , False ,
                  NULL ) ;

   /***=============================================================*/

   /*** label at top to let user know who we are ***/

   xstr = XmStringCreateLtoR( NO_DATASET_STRING ,
                              XmFONTLIST_DEFAULT_TAG ) ;
   info_lab = XtVaCreateManagedWidget(
                 "AFNI" , xmLabelWidgetClass , anat_rowcol ,
                    XmNlabelString , xstr ,
                    XmNrecomputeSize , False ,
                    XmNinitialResourcesPersistent , False ,
                 NULL ) ;
   XmStringFree(xstr) ;
   MCW_register_help( info_lab , "Shows dataset being rendered" ) ;

   /***** top row of widgets to choose dataset and sub-brick *****/

   SEP_HOR(anat_rowcol) ;  /* separator widget */

   hrc =  XtVaCreateWidget(
           "AFNI" , xmRowColumnWidgetClass , anat_rowcol ,
              XmNorientation  , XmHORIZONTAL ,
              XmNpacking      , XmPACK_TIGHT ,
              XmNadjustLast   , False ,
              XmNadjustMargin , False ,
              XmNtraversalOn  , False ,
              XmNmarginWidth  , 0 ,
              XmNmarginHeight , 0 ,
              XmNinitialResourcesPersistent , False ,
           NULL ) ;

   /*** button to let user choose dataset to render ***/

   xstr = XmStringCreateLtoR( "Choose Underlay Dataset" , XmFONTLIST_DEFAULT_TAG ) ;
   choose_pb = XtVaCreateManagedWidget(
                  "AFNI" , xmPushButtonWidgetClass , hrc ,
                     XmNalignment   , XmALIGNMENT_CENTER ,
                     XmNlabelString , xstr ,
                     XmNtraversalOn , False ,
                     XmNinitialResourcesPersistent , False ,
                  NULL ) ;
   XmStringFree(xstr) ;
   XtAddCallback( choose_pb, XmNactivateCallback, REND_choose_CB, NULL ) ;
   MCW_register_help( choose_pb ,
                      "Use this to popup a\n"
                      "'chooser' that lets\n"
                      "you select which\n"
                      "dataset to render."
                    ) ;

   /*** menu to let user choose sub-brick to deal with ***/

   SEP_VER(hrc) ;

   choose_av = new_MCW_arrowval(
                          hrc ,                   /* parent Widget */
                          "Brick " ,              /* label */
                          MCW_AV_optmenu ,        /* option menu style */
                          0 ,                     /* first option */
                          1 ,                     /* last option */
                          0 ,                     /* initial selection */
                          MCW_AV_readtext ,       /* ignored but needed */
                          0 ,                     /* decimal shift */
                          REND_choose_av_CB ,     /* callback when changed */
                          NULL ,                  /* data for above */
                          MCW_av_substring_CB ,   /* text creation routine */
                          REND_dummy_av_label     /* data for above */
                        ) ;

   /*** button to open and close overlay panel ***/

   SEP_VER(hrc) ;

   xstr = XmStringCreateLtoR( "Overlay" , XmFONTLIST_DEFAULT_TAG ) ;
   wfunc_open_pb = XtVaCreateManagedWidget(
                  "AFNI" , xmPushButtonWidgetClass , hrc ,
                     XmNalignment   , XmALIGNMENT_CENTER ,
                     XmNlabelString , xstr ,
                     XmNtraversalOn , False ,
                     XmNinitialResourcesPersistent , False ,
                  NULL ) ;
   XmStringFree(xstr) ;
   XtAddCallback( wfunc_open_pb, XmNactivateCallback, REND_open_func_CB, NULL ) ;

   XtManageChild(hrc) ;

   /***=============================================================*/

   /*** horizontal rowcol for data value clipping ***/

   SEP_HOR(anat_rowcol) ;  /* separator */

   hrc =  XtVaCreateWidget(
           "AFNI" , xmRowColumnWidgetClass , anat_rowcol ,
              XmNorientation  , XmHORIZONTAL ,
              XmNpacking      , XmPACK_TIGHT ,
              XmNadjustLast   , False ,
              XmNadjustMargin , False ,
              XmNtraversalOn  , False ,
              XmNmarginWidth  , 0 ,
              XmNmarginHeight , 0 ,
              XmNinitialResourcesPersistent , False ,
           NULL ) ;

   /*** vertical rowcol for dataset range information labels ***/

   vrc = XtVaCreateWidget(
             "AFNI" , xmRowColumnWidgetClass , hrc ,
                XmNpacking     , XmPACK_TIGHT ,
                XmNorientation , XmVERTICAL ,
                XmNadjustLast  , False ,
                XmNadjustMargin, False ,
                XmNtraversalOn , False ,
                XmNmarginWidth , 0 ,
                XmNmarginHeight, 0 ,
                XmNinitialResourcesPersistent , False ,
             NULL ) ;

   /*** 1st label for dataset range information ***/

   xstr = XmStringCreateLtoR( "Min=?????? Max=??????" , XmFONTLIST_DEFAULT_TAG ) ;
   range_lab = XtVaCreateManagedWidget(
                 "AFNI" , xmLabelWidgetClass , vrc ,
                    XmNlabelString , xstr ,
                    XmNrecomputeSize , False ,
                    XmNinitialResourcesPersistent , False ,
                 NULL ) ;
   XmStringFree(xstr) ;

   MCW_register_help( range_lab ,
                      "Shows the range of the data stored\n"
                      "in the brick voxels.\n"
                      "\n"
                      "N.B.: These values are NOT scaled\n"
                      "      by any floating point\n"
                      "      brick scaling factor."
                    ) ;

   /*** 2nd label for scaled dataset range information ***/

   xstr = XmStringCreateLtoR( "[123456789 123456789]" , XmFONTLIST_DEFAULT_TAG ) ;
   range_faclab = XtVaCreateWidget(
                    "AFNI" , xmLabelWidgetClass , vrc ,
                       XmNlabelString , xstr ,
                       XmNrecomputeSize , False ,
                       XmNinitialResourcesPersistent , False ,
                    NULL ) ;
   XmStringFree(xstr) ;

   MCW_register_help( range_faclab ,
                      "Shows the range of data stored\n"
                      "in the brick, this time multiplied\n"
                      "by the brick's scaling factor."
                    ) ;

   XtManageChild(vrc) ;

   SEP_VER(hrc) ;

   /*** arrowvals to get dataset clip levels ***/

   /*** vertical rowcol for Bot arrowval ***/

   vrc = XtVaCreateWidget(
             "AFNI" , xmRowColumnWidgetClass , hrc ,
                XmNpacking     , XmPACK_TIGHT ,
                XmNorientation , XmVERTICAL ,
                XmNadjustLast  , False ,
                XmNadjustMargin, False ,
                XmNtraversalOn , False ,
                XmNmarginWidth , 0 ,
                XmNmarginHeight, 0 ,
                XmNinitialResourcesPersistent , False ,
             NULL ) ;

   clipbot_av = new_MCW_arrowval( vrc , "Bot " ,
                                MCW_AV_downup , -CLIP_RANGE,CLIP_RANGE,-CLIP_RANGE ,
                                MCW_AV_editext , 0 ,
                                REND_clip_CB , NULL , NULL,NULL ) ;

   MCW_reghelp_children( clipbot_av->wrowcol ,
                         "All (unscaled) voxel values below\n"
                         "'Bot' will be increased to this\n"
                         "value.  The larger of 'Bot' and\n"
                         "'Min' is the left edge of the\n"
                         "brick graphs shown below."
                       ) ;

   xstr = XmStringCreateLtoR( "[-> 123456789]" , XmFONTLIST_DEFAULT_TAG ) ;
   clipbot_faclab = XtVaCreateWidget(
                    "AFNI" , xmLabelWidgetClass , vrc ,
                       XmNlabelString , xstr ,
                       XmNrecomputeSize , False ,
                       XmNinitialResourcesPersistent , False ,
                    NULL ) ;
   XmStringFree(xstr) ;

   MCW_register_help( clipbot_faclab ,
                      "Shows the scaled\nvalue of 'Bot'." ) ;

   XtManageChild(vrc) ;

   SEP_VER(hrc) ;

   /*** vertical rowcol for Top arrowval ***/

   vrc = XtVaCreateWidget(
             "AFNI" , xmRowColumnWidgetClass , hrc ,
                XmNpacking     , XmPACK_TIGHT ,
                XmNorientation , XmVERTICAL ,
                XmNadjustLast  , False ,
                XmNadjustMargin, False ,
                XmNtraversalOn , False ,
                XmNmarginWidth , 0 ,
                XmNmarginHeight, 0 ,
                XmNinitialResourcesPersistent , False ,
             NULL ) ;

   cliptop_av = new_MCW_arrowval( vrc , "Top " ,
                                MCW_AV_downup , -CLIP_RANGE,CLIP_RANGE, CLIP_RANGE ,
                                MCW_AV_editext , 0 ,
                                REND_clip_CB , NULL , NULL,NULL ) ;

   MCW_reghelp_children( cliptop_av->wrowcol ,
                         "All (unscaled) voxel values above\n"
                         "'Top' will be decreased to this\n"
                         "value.  The smaller of 'Top' and\n"
                         "'Max' is the right edge of the\n"
                         "brick graphs shown below."
                       ) ;

   xstr = XmStringCreateLtoR( "[-> 123456789]" , XmFONTLIST_DEFAULT_TAG ) ;
   cliptop_faclab = XtVaCreateWidget(
                    "AFNI" , xmLabelWidgetClass , vrc ,
                       XmNlabelString , xstr ,
                       XmNrecomputeSize , False ,
                       XmNinitialResourcesPersistent , False ,
                    NULL ) ;
   XmStringFree(xstr) ;

   MCW_register_help( clipbot_faclab ,
                      "Shows the scaled\nvalue of 'Top'." ) ;

   XtManageChild(vrc) ;
   XtManageChild(hrc) ;

   /***=============================================================*/

   /*** horizontal rowcol for graphs ***/

   SEP_HOR(anat_rowcol) ;  /* separator */

   hrc =  XtVaCreateWidget(
           "AFNI" , xmRowColumnWidgetClass , anat_rowcol ,
              XmNorientation , XmHORIZONTAL ,
              XmNpacking , XmPACK_TIGHT ,
              XmNadjustLast  , False ,
              XmNadjustMargin, False ,
              XmNtraversalOn , False ,
              XmNmarginWidth , 0 ,
              XmNmarginHeight, 0 ,
              XmNinitialResourcesPersistent , False ,
           NULL ) ;

   /*** graph to control grayscale ***/

   gry_graf = new_MCW_graf( hrc , im3d->dc, "Brightness", REND_graf_CB, NULL ) ;

   MCW_reghelp_children( gry_graf->topform ,
                         "This controls the brightness of each\n"
                         "voxel, as a function of input signal.\n"
                         "After you change this curve, you\n"
                         "must press 'Draw' to see the effect." ) ;

   SEP_VER(hrc) ;

   /*** graph to control opacity ***/

   opa_graf = new_MCW_graf( hrc , im3d->dc, "Opacity", REND_graf_CB, NULL ) ;

   MCW_reghelp_children( opa_graf->topform ,
                         "This controls the opacity of each\n"
                         "voxel, as a function of input signal.\n\n"
                         "After you change this curve, you\n"
                         "must press 'Draw' to see the effect." ) ;

   SEP_VER(hrc) ;

   /*** passive graph to show data distribution ***/

   his_graf = new_MCW_pasgraf( hrc , im3d->dc , "Sqrt Histogram" ) ;
   his_graf->mode = PASGRAF_BAR ;

   MCW_reghelp_children( his_graf->topform ,
                         "The graph height is proportional to\n"
                         "the square-root of the histogram of\n"
                         "the input signal.\n"
                         "\n"
                         "N.B.: The histogram at 0 is not included\n"
                         "      in the scaling, since it tends to\n"
                         "      be huge.  The square-root is graphed\n"
                         "      to enhance the range of the plot."      ) ;

   XtManageChild(hrc) ;

   /***=============================================================*/

   /*** horizontal rowcol to hold cutout controls ***/

   SEP_HOR(anat_rowcol) ;  /* separator */

   hrc =  XtVaCreateWidget(
           "AFNI" , xmRowColumnWidgetClass , anat_rowcol ,
              XmNorientation , XmHORIZONTAL ,
              XmNpacking , XmPACK_TIGHT ,
              XmNadjustLast  , False ,
              XmNadjustMargin, False ,
              XmNtraversalOn , False ,
              XmNmarginWidth , 0 ,
              XmNmarginHeight, 0 ,
              XmNinitialResourcesPersistent , False ,
           NULL ) ;

   /*** option menu to choose number of cutouts ***/

   numcutout_av = new_MCW_optmenu( hrc , "Cutouts " ,
                              0 , MAX_CUTOUTS , num_cutouts,0 ,
                              REND_numcutout_CB , NULL , NULL , NULL ) ;

   MCW_reghelp_children( numcutout_av->wrowcol ,
                         "Use this to choose the number of cutouts\n"
                         "to apply before rendering.  Controls for\n"
                         "the number selected will be activated below."
                       ) ;

   /*** option menu to choose cutout logic ***/

   logiccutout_av = new_MCW_optmenu( hrc , "+" ,
                              0 , 1 , logic_cutout,0 ,
                              NULL , NULL ,
                              MCW_av_substring_CB , cutout_logic_labels ) ;

   MCW_reghelp_children( logiccutout_av->wrowcol ,
                         "Use this to control the logic of how\n"
                         "multiple cutouts are combined:\n\n"
                         "OR  = the union of all regions\n"
                         "AND = the intersection of all regions"
                       ) ;

   SEP_VER(hrc) ;  /* separator */

   /*** arrowval to select opacity reduction factor ***/

   opacity_scale_av = new_MCW_arrowval( hrc , "Opacity Factor " ,
                                MCW_AV_downup , 0,10,10 ,
                                MCW_AV_noactext , 1 ,
                                REND_opacity_scale_CB , NULL , NULL,NULL ) ;
   XtAddCallback( opacity_scale_av->wtext, XmNactivateCallback,
                  REND_textact_CB, opacity_scale_av ) ;

   XtManageChild(hrc) ;

   /*** Create the widgets for each cutout ***/

   for( ii=0 ; ii < MAX_CUTOUTS ; ii++ ) cutouts[ii] = REND_make_cutout(ii) ;

   /***=============================================================*/

   /*** horizontal rowcol to hold automation controls ***/

   SEP_HOR(anat_rowcol) ;  /* separator */

   hrc =  XtVaCreateWidget(
           "AFNI" , xmRowColumnWidgetClass , anat_rowcol ,
              XmNorientation , XmHORIZONTAL ,
              XmNpacking , XmPACK_TIGHT ,
              XmNadjustLast  , False ,
              XmNadjustMargin, False ,
              XmNtraversalOn , False ,
              XmNmarginWidth , 0 ,
              XmNmarginHeight, 0 ,
              XmNinitialResourcesPersistent , False ,
           NULL ) ;

   /*** button box to enable automation mode ***/

   automate_bbox = new_MCW_bbox( hrc ,
                                 1 , automate_bbox_label ,
                                 MCW_BB_check , MCW_BB_noframe ,
                                 REND_autoflag_CB , NULL ) ;

   MCW_set_bbox( automate_bbox , automate_flag ) ;

   MCW_reghelp_children( automate_bbox->wrowcol ,
                         "IN:  Enable automation of renderings\n"
                         "OUT: Don't allow automated rendering"  ) ;

   SEP_VER(hrc) ;  /* separator */

   /*** arrowval to control number of frames to compute */

   autoframe_av = new_MCW_arrowval( hrc , "Frames " ,
                                    MCW_AV_downup , 2,999,5 ,
                                    MCW_AV_editext , 0 ,
                                    NULL , NULL , NULL,NULL ) ;

   MCW_reghelp_children( autoframe_av->wrowcol ,
                         "Use this to set the number\n"
                         "of frames that will be rendered\n"
                         "when 'Compute' is activated."     ) ;

   SEP_VER(hrc) ;  /* separator */

   /*** pushbutton to activate the automation ***/

   xstr = XmStringCreateLtoR( "Compute" , XmFONTLIST_DEFAULT_TAG ) ;
   autocompute_pb = XtVaCreateManagedWidget(
                     "AFNI" , xmPushButtonWidgetClass , hrc ,
                        XmNlabelString , xstr ,
                        XmNtraversalOn , False ,
                        XmNinitialResourcesPersistent , False ,
                     NULL ) ;
   XmStringFree(xstr) ;
   XtAddCallback( autocompute_pb, XmNactivateCallback, REND_autocompute_CB, NULL ) ;
   MCW_register_help( autocompute_pb ,
                      "Use this to start the\n"
                      "automation of rendering" ) ;

   /*** pushbutton to cancel the automation [not managed now] ***/

   xstr = XmStringCreateLtoR( " * CANCEL * " , XmFONTLIST_DEFAULT_TAG ) ;
   autocancel_pb = XtVaCreateWidget(
                     "AFNI" , xmPushButtonWidgetClass , hrc ,
                        XmNlabelString , xstr ,
                        XmNtraversalOn , False ,
                        XmNinitialResourcesPersistent , False ,
                     NULL ) ;
   XmStringFree(xstr) ;
   XtAddCallback( autocancel_pb, XmNactivateCallback, REND_autocancel_CB, NULL ) ;

   XtManageChild(hrc) ;

   /***=============================================================*/

   /*** horizontal rowcol to hold miscellaneous display controls ***/

   SEP_HOR(anat_rowcol) ;  /* separator */

   hrc =  XtVaCreateWidget(
           "AFNI" , xmRowColumnWidgetClass , anat_rowcol ,
              XmNorientation , XmHORIZONTAL ,
              XmNpacking , XmPACK_TIGHT ,
              XmNadjustLast  , False ,
              XmNadjustMargin, False ,
              XmNtraversalOn , False ,
              XmNmarginWidth , 0 ,
              XmNmarginHeight, 0 ,
              XmNinitialResourcesPersistent , False ,
           NULL ) ;

   /*** option menu to choose precalculation level ***/

   precalc_av = new_MCW_optmenu( hrc , "Precalc " ,
                              0 , NUM_precalc-1 , precalc_ival,0 ,
                              REND_precalc_CB , NULL ,
                              MCW_av_substring_CB , precalc_strings ) ;

   MCW_reghelp_children( precalc_av->wrowcol ,
                         "Use this to set the amount of precalculation\n"
                         "performed before rendering.  The higher levels\n"
                         "will speed drawing, but will require overhead\n"
                         "anytime the dataset, colors, or opacity are\n"
                         "modified (i.e., anything but the angles).\n"
                         "\n"
                         "Low    = slow rendering, no precalculation\n"
                         "Medium = faster rendering, some precalculation\n"
                         "High   = fastest rendering, much precalculation"
                       ) ;

   SEP_VER(hrc) ;  /* separator */

   /*** button box to show AFNI crosshair location ***/

   xhair_bbox = new_MCW_bbox( hrc ,
                              1 , xhair_bbox_label ,
                              MCW_BB_check , MCW_BB_noframe ,
                              REND_xhair_CB , NULL ) ;

   MCW_set_bbox( xhair_bbox , xhair_flag ) ;

   MCW_reghelp_children( xhair_bbox->wrowcol ,
                         "IN:  show AFNI crosshair location\n"
                         "OUT: don't show AFNI crosshairs\n"
                         "\n"
                         "N.B.: Must press Reload to see the\n"
                         "      crosshair position updated\n"
                         "      if it is changed in AFNI."
                       ) ;

   SEP_VER(hrc) ;  /* separator */

   /*** button box to do dynamic updates ***/

   dynamic_bbox = new_MCW_bbox( hrc ,
                                1 , dynamic_bbox_label ,
                                MCW_BB_check , MCW_BB_noframe ,
                                REND_dynamic_CB , NULL ) ;

   MCW_set_bbox( dynamic_bbox , dynamic_flag ) ;

   MCW_reghelp_children( dynamic_bbox->wrowcol ,
                         "IN:  Redraw immediately upon changes\n"
                         "OUT: Redraw only when commanded\n"
                         "\n"
                         "N.B.: Changes to the AFNI crosshair\n"
                         "      position are not detectable\n"
                         "      to force a dynamic redraw."     ) ;

   SEP_VER(hrc) ;  /* separator */

   /*** button box to accumulate images ***/

   accum_bbox = new_MCW_bbox( hrc ,
                              1 , accum_bbox_label ,
                              MCW_BB_check , MCW_BB_noframe ,
                              REND_accum_CB , NULL ) ;

   MCW_set_bbox( accum_bbox , accum_flag ) ;

   MCW_reghelp_children( accum_bbox->wrowcol ,
                         "IN:  Accumulate images for viewing\n"
                         "OUT: Save only the latest images"     ) ;

   XtManageChild(hrc) ;

   /***=============================================================*/

   /*** horizontal rowcol to hold angle arrows ***/

   SEP_HOR(anat_rowcol) ;  /* separator widget */

   hrc =  XtVaCreateWidget(
           "AFNI" , xmRowColumnWidgetClass , anat_rowcol ,
              XmNorientation , XmHORIZONTAL ,
              XmNpacking , XmPACK_TIGHT ,
              XmNadjustLast  , False ,
              XmNadjustMargin, False ,
              XmNtraversalOn , False ,
              XmNmarginWidth , 0 ,
              XmNmarginHeight, 0 ,
              XmNinitialResourcesPersistent , False ,
           NULL ) ;

   /***  arrowvals to choose rotation angles  ***/

   roll_av = new_MCW_arrowval( hrc , "Roll " ,
                                MCW_AV_downup , -999999,999999,(int)(0.1*angle_roll) ,
                                MCW_AV_noactext , -1 ,
                                REND_angle_CB , NULL , NULL,NULL ) ;
   roll_av->fstep = angle_fstep ;
   MCW_reghelp_children( roll_av->wrowcol ,
                         "Use this to set the roll angle\n"
                         "(about the I-S axis) for viewing,\n"
                         "then press 'Draw'"
                       ) ;
   XtAddCallback( roll_av->wtext, XmNactivateCallback, REND_textact_CB, roll_av ) ;

   SEP_VER(hrc) ;  /* separator widget */

   pitch_av = new_MCW_arrowval( hrc , "Pitch " ,
                                MCW_AV_downup , -999999,999999,(int)(0.1*angle_pitch) ,
                                MCW_AV_noactext , -1 ,
                                REND_angle_CB , NULL , NULL,NULL ) ;
   pitch_av->fstep = angle_fstep ;
   MCW_reghelp_children( pitch_av->wrowcol ,
                         "Use this to set the pitch angle\n"
                         "(about the R-L axis) for viewing,\n"
                         "then press 'Draw'"
                       ) ;
   XtAddCallback( pitch_av->wtext, XmNactivateCallback, REND_textact_CB, pitch_av ) ;

   SEP_VER(hrc) ;  /* separator widget */

   yaw_av = new_MCW_arrowval( hrc , "Yaw " ,
                                MCW_AV_downup , -999999,999999,(int)(0.1*angle_yaw) ,
                                MCW_AV_noactext , -1 ,
                                REND_angle_CB , NULL , NULL,NULL ) ;
   yaw_av->fstep = angle_fstep ;
   MCW_reghelp_children( yaw_av->wrowcol ,
                         "Use this to set the yaw angle\n"
                         "(about the A-P axis) for viewing,\n"
                         "then press 'Draw'"
                       ) ;
   XtAddCallback( yaw_av->wtext, XmNactivateCallback, REND_textact_CB, yaw_av ) ;

   XtManageChild(hrc) ;

   /***=============================================================*/

   /*** a set of action buttons below the line ***/

   SEP_HOR(anat_rowcol) ;

   (void) MCW_action_area( anat_rowcol , REND_actor , NACT ) ;

   help_pb   = (Widget) REND_actor[0].data ;
   draw_pb   = (Widget) REND_actor[1].data ;
   reload_pb = (Widget) REND_actor[2].data ;
   done_pb   = (Widget) REND_actor[3].data ;

   /***=============================================================*/

   /*** that's all [overlay widgets are only made upon demand] ***/

   XtManageChild(anat_rowcol) ;
   XtManageChild(anat_frame) ;

   XtManageChild(top_rowcol) ;
   XtRealizeWidget(shell) ;      /* will not be mapped */

#if 0
   XtVaSetValues( anat_rowcol       , XmNresizeWidth , False , NULL ) ;
#endif
   return ;
}

/*-------------------------------------------------------------------
  Make a line of cutout widgets
---------------------------------------------------------------------*/

#define NUM_CUTOUT_TYPES  21

static char * cutout_type_labels[NUM_CUTOUT_TYPES] = {
  "No Cut"       ,
  "Right of"     , "Left of"      ,
  "Anterior to"  , "Posterior to" ,
  "Inferior to"  , "Superior to"  ,
  "Expr > 0"     , "TT Ellipsoid" ,

  "Behind AL-PR" , "Front AL-PR"  ,    /* "x+y > val"    , "x+y < val"    , */
  "Front AR-PL"  , "Behind AR-PL" ,    /* "x-y > val"    , "x-y < val"    , */
  "Above AS-PI"  , "Below AS-PI"  ,    /* "y+z > val"    , "y+z < val"    , */
  "Below AI-PS"  , "Above AI-PS"  ,    /* "y-z > val"    , "y-z < val"    , */
  "Above RS-LI"  , "Below RS-LI"  ,    /* "x+z > val"    , "x+z < val"    , */
  "Below RI-LS"  , "Above RI-LS"       /* "x-z > val"    , "x-z < val"      */
} ;

static char * cutout_param_labels[NUM_CUTOUT_TYPES] = {
  "Parameter:   " ,
  "x(-R+L) [mm]:" , "x(-R+L) [mm]:" ,
  "y(-A+P) [mm]:" , "y(-A+P) [mm]:" ,
  "z(-I+S) [mm]:" , "z(-I+S) [mm]:" ,
  "Expression:  " , "Percentage:  " ,

  "Value [mm]:" , "Value [mm]:" ,
  "Value [mm]:" , "Value [mm]:" ,
  "Value [mm]:" , "Value [mm]:" ,
  "Value [mm]:" , "Value [mm]:" ,
  "Value [mm]:" , "Value [mm]:" ,
  "Value [mm]:" , "Value [mm]:"
} ;

#define CUT_NONE           0
#define CUT_RIGHT_OF       1
#define CUT_LEFT_OF        2
#define CUT_ANTERIOR_TO    3
#define CUT_POSTERIOR_TO   4
#define CUT_INFERIOR_TO    5
#define CUT_SUPERIOR_TO    6
#define CUT_EXPRESSION     7
#define CUT_TT_ELLIPSOID   8

#define CUT_SLANT_XPY_GT   9   /* slant cuts added 17 Feb 1999 */
#define CUT_SLANT_XPY_LT  10
#define CUT_SLANT_XMY_GT  11
#define CUT_SLANT_XMY_LT  12
#define CUT_SLANT_YPZ_GT  13
#define CUT_SLANT_YPZ_LT  14
#define CUT_SLANT_YMZ_GT  15
#define CUT_SLANT_YMZ_LT  16
#define CUT_SLANT_XPZ_GT  17
#define CUT_SLANT_XPZ_LT  18
#define CUT_SLANT_XMZ_GT  19
#define CUT_SLANT_XMZ_LT  20

#define CUT_SLANT_BASE     9
#define CUT_SLANT_NUM     12

#define SQ2 0.7071
static float cut_slant_normals[CUT_SLANT_NUM][3] = {
    { SQ2 , SQ2 , 0.0 } , {-SQ2 ,-SQ2 , 0.0 } ,
    { SQ2 ,-SQ2 , 0.0 } , {-SQ2 , SQ2 , 0.0 } ,
    { 0.0 , SQ2 , SQ2 } , { 0.0 ,-SQ2 ,-SQ2 } ,
    { 0.0 , SQ2 ,-SQ2 } , { 0.0 ,-SQ2 ,+SQ2 } ,
    { SQ2 , 0.0 , SQ2 } , {-SQ2 , 0.0 ,-SQ2 } ,
    { SQ2 , 0.0 ,-SQ2 } , {-SQ2 , 0.0 , SQ2 }
} ;
static int cut_slant_sign[CUT_SLANT_NUM] = {
    1 , -1 , 1 , -1 , 1 , -1 ,
    1 , -1 , 1 , -1 , 1 , -1
} ;

REND_cutout * REND_make_cutout( int n )
{
   XmString xstr ;
   char      str[64] ;
   REND_cutout * rc ;

   rc = myXtNew(REND_cutout) ;

   /* horizontal rowcol holds all that follows */

   rc->hrc =  XtVaCreateWidget(
                "AFNI" , xmRowColumnWidgetClass , anat_rowcol ,
                   XmNorientation , XmHORIZONTAL ,
                   XmNpacking , XmPACK_TIGHT ,
                   XmNadjustLast  , False ,
                   XmNadjustMargin, False ,
                   XmNtraversalOn , False ,
                   XmNmarginWidth , 0 ,
                   XmNmarginHeight, 0 ,
                   XmNinitialResourcesPersistent , False ,
                NULL ) ;

   /* menu to choose type of cutout */

   sprintf(str,"#%d",n+1) ;
   rc->type_av = new_MCW_optmenu( rc->hrc , str ,
                                  0 , NUM_CUTOUT_TYPES-1 , CUT_NONE,0 ,
                                  REND_cutout_type_CB , NULL ,
                                  MCW_av_substring_CB , cutout_type_labels ) ;
   if( NUM_CUTOUT_TYPES >= COLSIZE )
      AVOPT_columnize( wfunc_colornum_av , 1+(NUM_CUTOUT_TYPES+1)/COLSIZE ) ;

   MCW_reghelp_children( rc->type_av->wrowcol ,
                         "Use this to set the type of cutout\n"
                         "controlled by this line of inputs."  ) ;

   /* label to indicate parameter to enter */

   xstr = XmStringCreateLtoR( cutout_param_labels[0] , XmFONTLIST_DEFAULT_TAG ) ;
   rc->param_lab = XtVaCreateWidget(
                     "AFNI" , xmLabelWidgetClass , rc->hrc ,
                        XmNlabelString , xstr ,
                        XmNinitialResourcesPersistent , False ,
                     NULL ) ;
   XmStringFree(xstr) ;

   /* arrowval to enter parameter */

   rc->param_av = new_MCW_arrowval( rc->hrc , NULL ,
                                MCW_AV_downup , -999999,999999,0 ,
                                MCW_AV_noactext , -1 ,
                                REND_param_CB , NULL , NULL,NULL ) ;
   rc->param_av->fstep = cutout_fstep ;
   XtAddCallback( rc->param_av->wtext, XmNactivateCallback, REND_textact_CB, rc->param_av ) ;
   XtUnmanageChild( rc->param_av->wrowcol ) ;

   /* button to "Get" parameter from AFNI */

   xstr = XmStringCreateLtoR( "Get" , XmFONTLIST_DEFAULT_TAG ) ;
   rc->set_pb = XtVaCreateWidget(
                  "AFNI" , xmPushButtonWidgetClass , rc->hrc ,
                     XmNlabelString , xstr ,
                     XmNtraversalOn , False ,
                     XmNinitialResourcesPersistent , False ,
                  NULL ) ;
   XmStringFree(xstr) ;
   XtAddCallback( rc->set_pb, XmNactivateCallback, REND_cutout_set_CB, NULL ) ;
   MCW_register_help( rc->set_pb , "Use this to get the parameter\n"
                                   "for this cutout from the current\n"
                                   "AFNI crosshair location."           ) ;

   /* button box to allow "must do" status (overriding "AND") */

   rc->mustdo_bbox = new_MCW_bbox( rc->hrc ,
                                   1 , mustdo_bbox_label ,
                                   MCW_BB_check , MCW_BB_noframe ,
                                   NULL , NULL ) ;

   MCW_set_bbox( rc->mustdo_bbox , 0 ) ;

   MCW_reghelp_children( rc->mustdo_bbox->wrowcol ,
                         "Use this to force the cutout\n"
                         "to be performed, even if the\n"
                         "chosen logic is 'AND'.  If the\n"
                         "logic is 'OR', this does nothing." ) ;

   XtUnmanageChild( rc->mustdo_bbox->wrowcol ) ;

   XtManageChild( rc->hrc ) ;
   return rc ;
}

/*-------------------------------------------------------------------
  Callback for done button
---------------------------------------------------------------------*/

static int quit_first = 1 ;

void REND_done_timeout_CB( XtPointer client_data , XtIntervalId * id )
{
   MCW_set_widget_label( done_pb , "done" ) ;
   quit_first = 1 ;
   return ;
}

void REND_done_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   /** like AFNI itself, require two quick presses to exit **/

   if( w == done_pb && quit_first ){
      MCW_set_widget_label( done_pb , "DONE " ) ;
      quit_first = 0 ;
      (void) XtAppAddTimeOut(
               XtWidgetToApplicationContext(done_pb) ,
               5000 , REND_done_timeout_CB , NULL ) ;
      return ;
   }

   REND_destroy_imseq() ;      /* destroy the image window */
   DESTROY_IMARR(renderings) ; /* destroy the images */

   if( wfunc_frame != NULL && XtIsManaged(wfunc_frame) )  /* close overlay */
      REND_open_func_CB(NULL,NULL,NULL) ;

   XtUnmapWidget( shell ) ; renderer_open = 0 ; imseq = NULL ;

   if( dset      != NULL ) dset      = NULL ;
   if( func_dset != NULL ) func_dset = NULL ;

   if( render_handle != NULL ){
      destroy_MREN_renderer(render_handle) ; render_handle = NULL ;
   }

   FREE_VOLUMES ; INVALIDATE_OVERLAY ;
   return ;
}

/*-------------------------------------------------------------------
   Load the data from the dataset into local arrays
---------------------------------------------------------------------*/

void REND_reload_dataset(void)
{
   int ii , nvox , vmin,vmax , cbot,ctop , ival,val , cutdone=0 ;
   float fac ;
   void * var ;
   byte * gar ;
   MRI_IMAGE * vim ;
   XmString xstr ;
   char str[64] ;

   MCW_invert_widget(reload_pb) ;        /* flash a signal */

   /* start by tossing any old data */

   FREE_VOLUMES ;

   /* make sure the dataset is in memory */

   DSET_load(dset) ;
   vim = DSET_BRICK(dset,dset_ival) ; nvox = vim->nvox ;
   var = DSET_ARRAY(dset,dset_ival) ; brickfac = DSET_BRICK_FACTOR(dset,dset_ival) ;

   /* find data range, clip it, convert to bytes */

   grim = mri_new_conforming( vim , MRI_byte ) ;  /* new data */
   gar  = MRI_BYTE_PTR(grim) ;

   switch( DSET_BRICK_TYPE(dset,dset_ival) ){

      case MRI_short:{
         short * sar = (short *) var ;

         vmin = vmax = sar[0] ;
         for( ii=1 ; ii < nvox ; ii++ ){
            val = sar[ii] ;
                 if( vmin > val ) vmin = val ;
            else if( vmax < val ) vmax = val ;
         }

         if( new_dset ){
            AV_assign_ival( clipbot_av , vmin ) ; cbot = vmin ;
            AV_assign_ival( cliptop_av , vmax ) ; ctop = vmax ;
         } else {
            cbot = MAX( clipbot_av->ival , vmin ) ;
            ctop = MIN( cliptop_av->ival , vmax ) ;
         }

         fac  = (ctop > cbot) ? 255.9/(ctop-cbot) : 1.0 ;
         for( ii=0 ; ii < nvox ; ii++ ){
            val  = sar[ii] ;
            ival = fac * (val-cbot) ; RANGE(ival,0,255) ; gar[ii] = ival ;
         }
      }
      break ;

      case MRI_byte:{
         byte * bar = (byte *) var ;

         vmin = vmax = bar[0] ;
         for( ii=1 ; ii < nvox ; ii++ ){
            val = bar[ii] ;
                 if( vmin > val ) vmin = val ;
            else if( vmax < val ) vmax = val ;
         }

         if( new_dset ){
            AV_assign_ival( clipbot_av , vmin ) ; cbot = vmin ;
            AV_assign_ival( cliptop_av , vmax ) ; ctop = vmax ;
         } else {
            cbot = MAX( clipbot_av->ival , vmin ) ;
            ctop = MIN( cliptop_av->ival , vmax ) ;
         }

         fac  = (ctop > cbot) ? 255.9/(ctop-cbot) : 1.0 ;
         for( ii=0 ; ii < nvox ; ii++ ){
            val  = bar[ii] ;
            ival = fac * (val-cbot) ; RANGE(ival,0,255) ; gar[ii] = ival ;
         }
      }
      break ;
   }

   /* set label showing data range */

   sprintf(str,"Min=%d Max=%d",vmin,vmax) ;
   xstr = XmStringCreateLtoR( str , XmFONTLIST_DEFAULT_TAG ) ;
   XtVaSetValues( range_lab , XmNlabelString , xstr , NULL ) ;
   XmStringFree(xstr) ;

   /* if brick is scaled, show the scaled labels */

   HIDE_SCALE ;

   if( brickfac != 0.0 && brickfac != 1.0 ){
      char minch[16] , maxch[16] ;

      AV_fval_to_char( vmin*brickfac , minch ) ;
      AV_fval_to_char( vmax*brickfac , maxch ) ;
      sprintf(str,"[%s %s]",minch,maxch) ;
      xstr = XmStringCreateLtoR( str , XmFONTLIST_DEFAULT_TAG ) ;
      XtVaSetValues( range_faclab , XmNlabelString , xstr , NULL ) ;
      XmStringFree(xstr) ;

      AV_fval_to_char( brickfac * clipbot_av->ival , minch ) ;
      sprintf(str,"[-> %s]",minch) ;
      xstr = XmStringCreateLtoR( str , XmFONTLIST_DEFAULT_TAG ) ;
      XtVaSetValues( clipbot_faclab , XmNlabelString , xstr , NULL ) ;
      XmStringFree(xstr) ;

      AV_fval_to_char( brickfac * cliptop_av->ival , maxch ) ;
      sprintf(str,"[-> %s]",maxch) ;
      xstr = XmStringCreateLtoR( str , XmFONTLIST_DEFAULT_TAG ) ;
      XtVaSetValues( cliptop_faclab , XmNlabelString , xstr , NULL ) ;
      XmStringFree(xstr) ;

      XtManageChild( range_faclab   ) ;
      XtManageChild( clipbot_faclab ) ;
      XtManageChild( cliptop_faclab ) ;
   } else {
      XtUnmanageChild( range_faclab   ) ;
      XtUnmanageChild( clipbot_faclab ) ;
      XtUnmanageChild( cliptop_faclab ) ;
   }

   FIX_SCALE_SIZE ;

   /* copy data into opacity */

   opim = mri_to_byte( grim ) ;

   {  int hist[256] , nvox = grim->nvox , htop , ii ;
      float ofac = current_cutout_state.opacity_scale ;

      /* do the histogram graph */

      MCW_histo_bytes( nvox , MRI_BYTE_PTR(grim) , hist ) ;

      htop = 0 ;
      for( ii=0 ; ii < GRAF_SIZE ; ii++ ){
         hist[ii] = hist[2*ii] + hist[2*ii+1]  ;            /* fold 2 into 1  */
         if( ii > 0 && hist[ii] > htop ) htop = hist[ii] ;  /* find max value */
      }

      if( htop == 0 ){
         set_MCW_pasgraf( his_graf , NULL ) ;
      } else {
         float scl = (GRAF_SIZE-0.44) / sqrt((double)htop) ;
         byte bhis[GRAF_SIZE] ;
         for( ii=0 ; ii < GRAF_SIZE ; ii++ )
            bhis[ii] = (hist[ii] > htop) ? GRAF_SIZE-1
                                         : (byte)(scl*sqrt((double)hist[ii])+0.49) ;

         set_MCW_pasgraf( his_graf , bhis ) ;
      }

      redraw_MCW_pasgraf( his_graf ) ;

      /* modify the grayscale */

      if( ! gry_graf->yeqx ){
         byte * bar=MRI_BYTE_PTR(grim) , * fun=gry_graf->func ;

         for( ii=0 ; ii < nvox ; ii++ ) bar[ii] = fun[ bar[ii] ] ;
      }

      /* modify the opacity */

      if( !opa_graf->yeqx || ofac < 1.0 ){
         byte * bar=MRI_BYTE_PTR(opim) , * fun=opa_graf->func ;

         if( !opa_graf->yeqx )
            for( ii=0 ; ii < nvox ; ii++ ) bar[ii] = fun[ bar[ii] ] ;

         if( ofac < 1.0 )
            for( ii=0 ; ii < nvox ; ii++ ) bar[ii] *= ofac ;
      }
   }

   /*--- Now deal with overlay, if need be ---*/

   func_computed = 0 ;  /* at this point, data is for grayscale rendering */

   if( DO_OVERLAY ){
      byte * gar , * opar , * ovar ;
      int nvox = grim->nvox , ii ;

      if( num_cutouts > 0 && !func_cut_overlay ){  /* do cutouts NOW if not */
         REND_cutout_blobs() ;                     /* to be done to overlay */
         cutdone = 1 ;
      }

      if( ovim == NULL ) REND_reload_func_dset() ;

      gar  = MRI_BYTE_PTR(grim) ;
      opar = MRI_BYTE_PTR(opim) ;
      ovar = MRI_BYTE_PTR(ovim) ;

      /* convert gar into index into the functional colormap */

      for( ii=0 ; ii < nvox ; ii++ ){
         if( ovar[ii] == 0 ) gar[ii] = gar[ii] >> 1   ;  /* gray */
         else                gar[ii] = 127 + ovar[ii] ;  /* color */
      }

      /* if needed, modify the opacity where there is color */

      if( func_color_opacity > 0.0 ){
         byte opac = (byte)(255.0 * func_color_opacity) ;
         for( ii=0 ; ii < nvox ; ii++ )
            if( ovar[ii] != 0 ) opar[ii] = opac ;
      }

      func_computed = 1 ;  /* data is now set for color rendering */
   }

   /*--- Other piddling details ---*/

   if( num_cutouts > 0 && !cutdone ) REND_cutout_blobs()  ; /* cutouts hit overlay */
   if( xhair_flag                  ) REND_xhair_overlay() ;

   MCW_invert_widget(reload_pb) ;  /* turn the signal off */

   new_dset = 0 ;
   return ;
}

/*-------------------------------------------------------------------
   Callback for the reload button
---------------------------------------------------------------------*/

void REND_reload_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   if( dset == NULL ){ XBell(dc->display,100) ; return ; }

   REND_reload_dataset() ;             /* load data again */

   if( render_handle != NULL ){
      REND_reload_renderer() ;         /* send it to renderer */
      post_facto_load = 0 ;
      REND_draw_CB(NULL,NULL,NULL) ;   /* redraw */
   } else {
      post_facto_load = 1 ;            /* need to remember this action */
   }

   return ;
}

/*-----------------------------------------------------------------------
  Actually send the computed bricks to the renderer
-------------------------------------------------------------------------*/

void REND_reload_renderer(void)
{
   if( render_handle == NULL ) return ;  /* error */

   if( func_computed ){

      if( ! func_cmap_set ){
         MREN_set_rgbmap( render_handle, func_ncmap, func_rmap,func_gmap,func_bmap ) ;
         func_cmap_set = 1 ;
      }

      MREN_set_rgbbytes( render_handle , grim ) ;  /* color overlay */

   } else {

      MREN_set_graybytes( render_handle , grim ) ; /* grayscale overlay */

   }

   MREN_set_opabytes( render_handle , opim ) ;     /* same for either case */
   return ;
}

/*-------------------------------------------------------------------
  Callback for draw button
---------------------------------------------------------------------*/

void REND_draw_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   MRI_IMAGE * rim ;

   if( dset == NULL ){ XBell(dc->display,100) ; return ; }

   MCW_invert_widget(draw_pb) ;

   /* if needed, create stuff for rendering */

   if( render_handle == NULL ){
      render_handle = new_MREN_renderer() ;
#ifdef REND_DEBUG
      MREN_be_verbose(render_handle) ;  /* for debugging */
#endif
   }

   REND_load_cutout_state() ;           /* load cutout data from widgets */

   if( REND_cutout_state_changed() ){   /* if cutouts changed, must reload data */
      FREE_VOLUMES ;
      if( func_cut_overlay ) INVALIDATE_OVERLAY ;
      old_cutout_state = current_cutout_state ;
   }

   if( xhair_flag && CHECK_XHAIR_MOTION ){  /* check for new crosshair position */
      FREE_VOLUMES ;
   }

   if( NEED_RELOAD ){
      REND_reload_dataset() ;
      REND_reload_renderer() ;
   } else if ( post_facto_load || MREN_needs_data(render_handle) ){
      REND_reload_renderer() ;
   }

   /* setup for viewing */

   angle_roll  = REND_evaluate( roll_av )  ;  /* read angles from arrowvals */
   angle_pitch = REND_evaluate( pitch_av ) ;
   angle_yaw   = REND_evaluate( yaw_av )   ;

   MREN_set_viewpoint( render_handle , -angle_yaw,-angle_pitch,-angle_roll ) ;
   MREN_set_precalculation( render_handle , precalc_mode[precalc_ival] ) ;
   MREN_set_min_opacity( render_handle ,
                         0.05 * current_cutout_state.opacity_scale ) ;

   /* create and display the rendered image */

   rim = MREN_render( render_handle , npixels ) ;

   if( rim == NULL ){
      (void) MCW_popup_message( draw_pb ,
                                   "** Rendering fails,    **\n"
                                   "** for unknown reasons **\n\n"
                                   "** Sorry -- RWCox      **\n" ,
                                MCW_USER_KILL | MCW_TIMER_KILL ) ;
      XBell(dc->display,100) ;
      MCW_invert_widget(draw_pb) ; return ;
   }

   if( accum_flag || automate_flag ){
      if( renderings == NULL ){ INIT_IMARR( renderings ) ; }
      ADDTO_IMARR( renderings , rim ) ;
   } else {
      DESTROY_IMARR( renderings ) ;
      INIT_IMARR( renderings ) ;
      ADDTO_IMARR( renderings , rim ) ;
   }
   REND_update_imseq() ;

   MCW_invert_widget(draw_pb) ;
   return ;
}

/*-------------------------------------------------------------------
  Callback for help button
---------------------------------------------------------------------*/

void REND_help_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   (void ) new_MCW_textwin( help_pb ,

       "++++++++++++++++++  V O L U M E   R E N D E R I N G  ++++++++++++++++++\n"
       "\n"
       "This plugin is used to render one brick from a 3D dataset in grayscale\n"
       "(the underlay), possibly overlaid in color with another (functional)\n"
       "dataset.  Although lengthy, this help is still rather terse.  Some\n"
       "experimentation will be needed to get decent results, since there are\n"
       "many controls that affect the way the final images appear.\n"
       "\n"
       "General Notes:\n"
       "--------------\n"
       " * To be rendered, a dataset must have cubical voxels, its\n"
       "     data must be stored as bytes or shorts (but may have\n"
       "     a floating point scaling factor attached), and must\n"
       "     be stored as axial slices in the 'RAI' orientation\n"
       "     (x axis is Right-to-Left, y axis is Anterior-to-Posterior,\n"
       "     and z axis is Inferior-to-Posterior).  This orientation\n"
       "     is how datasets are written out in the +acpc and +tlrc\n"
       "     coordinates.\n"
       "\n"
       " * Use the Draw button to force rendering after making changes\n"
       "     to the drawing parameters or closing the image window.\n"
       "\n"
       " * The 'Reload' button is used to re-copy the dataset brick into\n"
       "     the renderer.  This can be used if you are altering the\n"
       "     dataset interactively with the Draw Dataset plugin.\n"
       "     Otherwise, you probably don't need this, since the reload\n"
       "     operation will be carried out as needed by the renderer.\n"
       "\n"
       " * The Precalc mode determines how much work is done ahead of\n"
       "     time to make rendering more efficient:\n"
       "\n"
       "      Low    = no precalculation, very slow rendering\n"
       "                (this is the default on SGI machines);\n"
       "      Medium = small amount of precalculation, speeds up\n"
       "                rendering quite a lot on most computers\n"
       "                (this is the default on non-SGI machines);\n"
       "      High   = large amount of precalculation, can speed up\n"
       "                rendering even more, but will consume \n"
       "                several seconds and A LOT of memory in\n"
       "                the process.  This is useful if the only\n"
       "                changes between frames are viewing angles.\n"
       "\n"
       " * WARNING: The Medium and High mode rendering functions do not\n"
       "            work well on some computers, producing images with\n"
       "            a grid stippled on top.  This problem occurs with\n"
       "            some SGI compilers and (I've heard) some Sun systems.\n"
       "            In such a case, you may have to use the Low mode,\n"
       "            as painful as it is.  (If I knew how to fix this problem,\n"
       "            I would, so don't complain to me!)\n"
       "   N.B.: The Unix environment variable AFNI_RENDER_PRECALC_MODE can\n"
       "         be set to 'Low', 'Medium', or 'High' to override the default\n"
       "         initial setting.\n"
       "\n"
       " * If you depress 'See Xhairs', a 3D set of crosshairs\n"
       "     corresponding to the AFNI focus position will be drawn.\n"
       "     However, if you move the crosshairs in the AFNI image\n"
       "     windows, the rendering window is not automatically updated,\n"
       "     but the next time the rendering is redrawn for some other\n"
       "     reason, the correct crosshair positions will be shown.\n"
       "\n"
       " * If you depress 'DynaDraw', then the image will be re-\n"
       "     rendered immediately whenever certain actions are taken:\n"
       "      + A viewing angle is changed by pressing an arrow.\n"
       "      + A cutout parameter is changed by pressing an arrow or\n"
       "        a 'Get' button.\n"
       "      + The 'See Xhairs' toggle state is changed.\n"
       "     Changing one of these values by directly typing in the\n"
       "     corresponding text entry field will NOT force a redraw\n"
       "     even in DynaDraw mode -- you will have to press 'Draw'.\n"
       "     Other changes (e.g., altering the opacity graph) will\n"
       "     not force a dynamic redraw and also require the use\n"
       "     of the 'Draw' button.\n"
       "   N.B.: The data entry fields with which DynaDraw and Automate\n"
       "         interact are displayed with a raised border, unlike\n"
       "         other data entry fields (e.g., 'Bot').\n"
       "   N.B.: The default stepsize for the angle and cutout variables\n"
       "         when an arrow is pressed is 5.0.  These values can be\n"
       "         altered by setting the Unix environment variables\n"
       "         AFNI_RENDER_ANGLE_DELTA and AFNI_RENDER_CUTOUT_DELTA\n"
       "         prior to running AFNI.  Once AFNI is started, these\n"
       "         stepsizes are fixed.\n"
       "\n"
       " * If you depress 'Accumulate', then rendered images are\n"
       "     saved as they are computed and can be re-viewed in the\n"
       "     image display window.  If Accumulate is off, then only\n"
       "     the latest image is kept.\n"
       "\n"
       " * If you depress 'Automate', then the automatic generation\n"
       "     of renderings as some parameter varies is enabled.\n"
       "     The 'Frames' field controls how many renderings will\n"
       "     be made.  To vary some parameter, you type an\n"
       "     arithmetic expression in the variable 't' in the\n"
       "     parameter control field.  The parameters that can\n"
       "     be so varied are the viewing angles and the cutout\n"
       "     parameters (i.e., those whose data entry field is\n"
       "     drawn with a raised border).  For the first rendering\n"
       "     t=0, for the second t=1, etc., up to t=N-1, where N is\n"
       "     the number of frames ordered.  (You can also use the\n"
       "     variable 'N' in the parameter expressions.) Once the\n"
       "     expressions are set up, press 'Compute' to begin\n"
       "     rendering automation.  (Then go have a pumpernickel\n"
       "     bagel and a cup of lapsang souchong tea.)\n"
       "\n"
       " * Notes about Automate:\n"
       "   1) If none of the parameters has an expression involving\n"
       "       't', then each frame in the rendering will be identical,\n"
       "       but the program won't detect that and you will waste a\n"
       "       lot of CPU time and memory.\n"
       "   2) Use the same expression syntax as with program 3dcalc.\n"
       "       An illegal expression (e.g., '2+*3t') will silently\n"
       "       evaluate to zero.\n"
       "   3) It is legal to have more than one parameter depend on 't'.\n"
       "        For example, combining cutouts\n"
       "           Anterior To:  10+2*t\n"
       "           Posterior To: 20+2*t\n"
       "        with the OR logic produces a 10 mm thick coronal slice\n"
       "        that slides backwards at 2 mm per frame.\n"
       "   4) If Accumulate is on, then the frames created by automated\n"
       "       rendering will be added to the list of stored images.  If\n"
       "       Accumulate is off, the previously saved images will be\n"
       "       discarded, and only the newly generated image sequence will\n"
       "       be available for viewing.\n"
       "   5) There is no way to save an animation to disk as a unit.\n"
       "       However, you could use the 'Save:bkg' button on the image\n"
       "       viewer to save each image to disk in PNM format, then convert\n"
       "       the collection of individual image files to some movie format,\n"
       "       using software outside of the AFNI package.\n"
       "   6) Using an arrow to change a field with an expression\n"
       "       entered will result in the destruction of the expression\n"
       "       string and its replacement by a number.\n"
       "   7) At the end of an Automate run, you can convert a field\n"
       "       with an expression to its final numerical value by\n"
       "       clicking in the data field and then pressing the\n"
       "       Enter (or Return) key.  In combination with Accumulate,\n"
       "       this makes it easy to chain together the results of\n"
       "       multiple automated rendering computations, first varying\n"
       "       one parameter and then another.\n"
       "   8) During an Automate run, a 'CANCEL' button to the right\n"
       "       of 'Compute' becomes visible.  If you press this, then\n"
       "       the automation will be interrupted when it finishes the\n"
       "       image on which it is working (you have to wait until\n"
       "       that time -- pressing the button twice won't help!).\n"
       "\n"
       " * The Min= and Max= values show the range of numbers stored\n"
       "     in the dataset brick.  The brick is copied into internal\n"
       "     memory for rendering.  You can use the 'Bot' and 'Top'\n"
       "     controls to limit the range of the copied voxel data.\n"
       "     Anything below 'Bot' will be set to the Bot value, and\n"
       "     anything above 'Top' will be set to the Top value.\n"
       "     (If Bot < Min, then Bot is effectively equal to Min;\n"
       "      if Top > Max, then Top is effectively equal to Max.)\n"
       "\n"
       " * The 'Brightness' and 'Opacity' graphs are used to control\n"
       "     the mappings from dataset signal level (the numbers stored\n"
       "     in the voxels) to the grayscale and opacity levels.\n"
       "     The abscissa represents the copied voxel values, ranging\n"
       "     the larger of Min or Bot, to the smaller of Max or Top.\n"
       "     A larger opacity makes a voxel less transparent; for\n"
       "     example, a high opacity with a low brightness is like\n"
       "     black barrier in the line of sight.  Zero opacity\n"
       "     means a voxel is transparent and does not contribute to\n"
       "     the rendered image.\n"
       " * The ordinate on the 'Brightness' graph ranges from black\n"
       "     at the bottom to white at the top.  The ordinate on the\n"
       "     'Opacity' graph ranges from 0 (transparent) to 1 (opaque).\n"
       " * The 'Opacity Factor' control lets you scale the entire\n"
       "     underlay opacity down by some constant factor.  (However,\n"
       "     this doesn't seem to be very useful.)\n"
       "\n"
       " * The 'Cutouts' menu lets you select the number of regions to\n"
       "     be cut out of the volume prior to rendering (this is done\n"
       "     by setting the voxel opacity inside each cutout to zero).\n"
       "     Each cutout is controlled by a single parameter.  For\n"
       "     example, the parameter for 'Right of' cutout is an\n"
       "     x-coordinate, and all the voxels to the right of this\n"
       "     value will be included in the cutout.\n"
       "   N.B.: Right     (of midline)   = negative x  (+tlrc: to  -68)\n"
       "         Left      (of midline)   = positive x  (+tlrc: to  +68)\n"
       "         Anterior  (to the AC)    = negative y  (+tlrc: to  -70)\n"
       "         Posterior (to the AC)    = positive y  (+tlrc: to +102)\n"
       "         Inferior  (to the AC-PC) = negative z  (+tlrc: to  -42)\n"
       "         Superior  (to the AC-PC) = positive z  (+tlrc: to  +74)\n"
       "\n"
       " * The 'Expr > 0' cutout is a special (and slow) case. Instead\n"
       "     of a number, you enter an expression (using the 3dcalc\n"
       "     syntax) containing the symbols 'x', 'y', and 'z', which\n"
       "     represent the spatial coordinates of each voxel.  Voxels\n"
       "     where the expression evaluates to a positive number will\n"
       "     be cut out.  For example, '100-x*x-y*y-z*z' will cut out\n"
       "     the interior of a sphere of radius 10 mm, centered at the\n"
       "     origin of coordinates.\n"
       "\n"
       " * The 'TT Ellipsoid' cutout will remove all voxels exterior to\n"
       "     an ellipsoid that approximates the outer contour of the\n"
       "     Talairach-Tournoux atlas, when its 'Percentage' parameter\n"
       "     is set to 100.  Smaller percentages will shrink the ellipsoid\n"
       "     in towards the center of the brain.\n"
       "\n"
       " * The 'Behind', 'Above', etc. cutouts are relative to planes at\n"
       "     45 degrees to the standard views.  For example, 'Behind AL-PR'\n"
       "     cuts out behind a slice that starts Anterior-Left (AL) and\n"
       "     ends up at Posterior-Right (PR) -- halfway between a coronal\n"
       "     and a sagittal slice.  The simplest way to set the value\n"
       "     parameter for these (at least to start) is to use 'Get'.\n"
       "\n"
       " * Cutouts can be combined with the 'OR' logic, which means that\n"
       "     the union of all the specified cutout regions will be\n"
       "     removed.  They can also be combined with the 'AND' logic,\n"
       "     which means that the intersection of the cutout regions\n"
       "     will be removed.  If 'AND' is selected, a cutout can be\n"
       "     forced to be removed in its entirety using the 'Must Do'\n"
       "     control.  (That is, 'AND' only applies to those that\n"
       "     do NOT have 'Must Do' selected.)  By combining cutouts\n"
       "     cleverly, you can produce some pretty funky brain images.\n"
       "\n"
       "Color Overlays\n"
       "--------------\n"
       "By pressing the 'Overlay' button, you can access the controls for\n"
       "displaying a second dataset in color on top of the underlay dataset.\n"
       "The controls are designed to be similar to the controls in the\n"
       "'Define Function' control panel in a main AFNI window, and so only\n"
       "the principal differences will be explained here.\n"
       "\n"
       " * One brick ('Color') is chosen to determine the colors shown, and\n"
       "     another brick ('Thr') to determine which voxels from the overlay\n"
       "     dataset will be shown at all.  If you don't want thresholding\n"
       "     applied, just set the threshold slider down to 0.\n"
       "\n"
       " * The 'Color Opacity' control determines how opaque each supra-\n"
       "     threshold voxel will be.  If it is set to 'Underlay', then\n"
       "     the opacity of a colored voxel will be determined from the\n"
       "     underlay opacity at that location (before cutouts are applied).\n"
       "\n"
       " * 'See Overlay' is used to toggle the color overlay computations\n"
       "     on and off - it should be pressed IN for the overlay to become\n"
       "     visible.\n"
       "\n"
       " * 'Cutout Overlay' determines if the cutout operations affect the\n"
       "     overlaid voxels.  If it is pressed IN, then cutouts will include\n"
       "     the overlay; if OUT, then colored voxels will hang free in\n"
       "     empty space where the underlay was cutout beneath them.\n"
       "   N.B.: If 'Color Opacity' is set to 'Underlay', the cutouts will\n"
       "         hit the overlay in all cases, since cutouts are implemented\n"
       "         by setting the opacity of the underlay to zero in the chosen\n"
       "         regions.\n"
       "\n"
       " * 'Remove Isolas', if pressed IN, will cause single isolated supra-\n"
       "     threshold voxels to be removed before rendering.\n"
       "\n"
       " * None of the overlay controls are hooked up to 'DynaDraw' or to\n"
       "     'Automate'.  You must manually press 'Draw' to see the effects\n"
       "     of changes to these settings.\n"
       "\n"
       " * A slightly different colormap is used when rendering overlays than\n"
       "     when only underlays are visible.  The result is that the grayscale\n"
       "     underlay will look a little different between the 'See Overlay'\n"
       "     IN and OUT conditions.  Also, the colormaps are rendered into\n"
       "     24 bit RGB images, which might not be faithfully displayed in\n"
       "     the image window.  If the 'Save:bkg' button is used to save\n"
       "     a set of RGB images, they will be saved in their internal color\n"
       "     resolution (in PPM format); they might appear slightly different\n"
       "     when viewed external to AFNI (e.g., using program xv).\n"
       "   N.B.: When viewing an RGB image, most of the image processing\n"
       "         options available from the 'Disp' control panel do not\n"
       "         function.  'Sharpen' still works.\n"
       "\n"
       "Other Notes:\n"
       "------------\n"
       " * The rendering is done using the VolPack library from Stanford,\n"
       "     by Philippe Lacroute (http://www-graphics.stanford.edu).\n"
       "     This library behaves peculiarly on some SGI systems (perhaps\n"
       "     due to compiler bugs) , and may produce incorrect images\n"
       "     in 'Medium' and 'High' modes.  'Low' mode seem always to work\n"
       "     well, but is unfortunately quite slow.\n"
       "\n"
       " * The images produced may look a little blurry, due to the linear\n"
       "     interpolation scheme used by VolPack.  You can use the 'Sharpen'\n"
       "     option on the image viewer 'Disp' control panel to make them\n"
       "     look a little nicer.\n"
       "\n"
       " * This plugin is very CPU and memory intensive, and will not run\n"
       "     at all decently on a computer with less than 128 MB of RAM.\n"
       "------------------------------------------------------------------------\n"
       "\n"
       "RW Cox, Milwaukee - February 1999\n"

    , TEXT_READONLY ) ;
   return ;
}

/*-------------------------------------------------------------------
  Callback for (underlay) choose button.
  Criteria for datasets that can be rendered:
    - must be in current session
    - must have actual bricks of bytes or shorts
    - bricks must have cubical grid
    - bricks must be in RAI orientation
---------------------------------------------------------------------*/

#define USEFUL_DSET(ds) (( ISVALID_DSET(ds)                      ) && \
                         ( DSET_INMEMORY(ds)                     ) && \
                         ( DSET_CUBICAL(ds)                      ) && \
                         ( DSET_BRICK_TYPE(ds,0) == MRI_short ||      \
                           DSET_BRICK_TYPE(ds,0) == MRI_byte     ) && \
                         ( (ds)->daxes->xxorient == ORI_R2L_TYPE ) && \
                         ( (ds)->daxes->yyorient == ORI_A2P_TYPE ) && \
                         ( (ds)->daxes->zzorient == ORI_I2S_TYPE )   )

static int                  ndsl = 0 ;
static PLUGIN_dataset_link * dsl = NULL ;

void REND_load_dsl( THD_3dim_dataset * mset )
{
   THD_session * ss  = im3d->ss_now ;           /* current session */
   int           vv  = im3d->vinfo->view_type ; /* view type */
   THD_3dim_dataset * qset ;
   int id , nx,ny,nz ;

   ndsl = 0 ; /* initialize */

   if( ISVALID_DSET(mset) ){
      nx = DSET_NX(mset) ; ny = DSET_NY(mset) ; nz = DSET_NZ(mset) ;
   } else {
      nx = ny = nz = 0 ;
   }

   /* scan anats */

   for( id=0 ; id < ss->num_anat ; id++ ){
      qset = ss->anat[id][vv] ;

      if( ! USEFUL_DSET(qset) ) continue ;   /* skip this one */

      if( nx > 0 && DSET_NX(qset) != nx ) continue ;  /* must match */
      if( ny > 0 && DSET_NY(qset) != ny ) continue ;  /* brick size */
      if( nz > 0 && DSET_NZ(qset) != nz ) continue ;

      ndsl++ ;
      dsl = (PLUGIN_dataset_link *)
              XtRealloc( (char *) dsl , sizeof(PLUGIN_dataset_link)*ndsl ) ;

      make_PLUGIN_dataset_link( qset , dsl + (ndsl-1) ) ;  /* cf. afni_plugin.c */
   }

   /* scan funcs */

   for( id=0 ; id < ss->num_func ; id++ ){
      qset = ss->func[id][vv] ;

      if( ! USEFUL_DSET(qset) ) continue ;   /* skip this one */

      if( nx > 0 && DSET_NX(qset) != nx ) continue ;
      if( ny > 0 && DSET_NY(qset) != ny ) continue ;
      if( nz > 0 && DSET_NZ(qset) != nz ) continue ;

      ndsl++ ;
      dsl = (PLUGIN_dataset_link *)
              XtRealloc( (char *) dsl , sizeof(PLUGIN_dataset_link)*ndsl ) ;

      make_PLUGIN_dataset_link( qset , dsl + (ndsl-1) ) ;
   }

   return ;
}

void REND_choose_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   int vv = im3d->vinfo->view_type ; /* view type */
   THD_3dim_dataset * qset ;
   int id , ltop , llen , dofunc ;
   char qnam[THD_MAX_NAME] , label[THD_MAX_NAME] ;
   static char ** strlist = NULL ;

   dofunc = (w == wfunc_choose_pb) ;

   if( dofunc && !ISVALID_DSET(dset) ){
      (void) MCW_popup_message( w ,
                                   "Can't choose overlay\nbefore underlay!" ,
                                MCW_USER_KILL | MCW_TIMER_KILL ) ;
      XBell(dc->display,100) ; return ;
   }

   REND_load_dsl( (dofunc) ? dset : NULL ) ;

   /* found nothing?  exit */

   if( ndsl < 1 ){
      (void) MCW_popup_message( w ,
                                   "Didn't find\nany datasets\nto render!" ,
                                MCW_USER_KILL | MCW_TIMER_KILL ) ;
      XBell(dc->display,100) ; return ;
   }

   /*--- loop over dataset links and patch their titles
         to include an indicator of the dataset type    ---*/

   ltop = 4 ;
   for( id=0 ; id < ndsl ; id++ ){
      llen = strlen(dsl[id].title) ;
      ltop = MAX(ltop,llen) ;
   }

   for( id=0 ; id < ndsl ; id++ ){
      qset = PLUTO_find_dset( &(dsl[id].idcode) ) ;
      if( ! ISVALID_DSET(qset) ) continue ;
      if( ISANAT(qset) ){
         if( ISANATBUCKET(qset) )         /* 30 Nov 1997 */
            sprintf(qnam,"%-*s [%s:%d]" ,
                    ltop,dsl[id].title ,
                    ANAT_prefixstr[qset->func_type] , DSET_NVALS(qset) ) ;

         else if( DSET_NUM_TIMES(qset) == 1 )
            sprintf(qnam,"%-*s [%s]" ,
                    ltop,dsl[id].title ,
                    ANAT_prefixstr[qset->func_type] ) ;

         else
            sprintf(qnam,"%-*s [%s:3D+t:%d]" ,
                    ltop,dsl[id].title ,
                    ANAT_prefixstr[qset->func_type] , DSET_NUM_TIMES(qset) ) ;

      } else {
         if( ISFUNCBUCKET(qset) )         /* 30 Nov 1997 */
            sprintf(qnam,"%-*s [%s:%d]" ,
                    ltop,dsl[id].title ,
                    FUNC_prefixstr[qset->func_type] , DSET_NVALS(qset) ) ;

         else if( DSET_NUM_TIMES(qset) == 1 )
            sprintf(qnam,"%-*s [%s]" ,
                    ltop,dsl[id].title ,
                    FUNC_prefixstr[qset->func_type] ) ;

         else
            sprintf(qnam,"%-*s [%s:3D+t:%d]" ,
                    ltop,dsl[id].title ,
                    FUNC_prefixstr[qset->func_type] , DSET_NVALS(qset) ) ;
      }

      if( DSET_COMPRESSED(qset) ) strcat(qnam,"z") ;

      strcpy( dsl[id].title , qnam ) ;
   }

   /*--- make a popup chooser for the user to browse ---*/

   POPDOWN_strlist_chooser ;

   strlist = (char **) XtRealloc( (char *)strlist , sizeof(char *)*ndsl ) ;
   for( id=0 ; id < ndsl ; id++ ) strlist[id] = dsl[id].title ;

   sprintf( label , "AFNI Dataset from\nthe %s" , VIEW_typestr[vv] ) ;

   MCW_choose_strlist( w , label , ndsl , -1 , strlist ,
                       (dofunc) ? REND_finalize_func_CB
                                : REND_finalize_dset_CB , NULL ) ;

   return ;
}

void REND_finalize_dset_CB( Widget w, XtPointer fd, MCW_choose_cbs * cbs )
{
   int id = cbs->ival ;
   THD_3dim_dataset * qset ;
   XmString xstr ;
   char str[2*THD_MAX_NAME] ;
   float fac ;

   /* check for errors */

   if( ! renderer_open ){ POPDOWN_strlist_chooser ; XBell(dc->display,100) ; return ; }

   if( id < 0 || id >= ndsl ){ XBell(dc->display,100) ; return ; }

   qset = PLUTO_find_dset( &(dsl[id].idcode) ) ;  /* the new dataset? */

   if( qset == NULL ){ XBell(dc->display,100) ; return ; }

   if( ! DSET_CUBICAL(qset) ){ XBell(dc->display,100) ; return ; }

   /* if there was an existing renderer, kill it off */

   if( render_handle != NULL ){
      destroy_MREN_renderer(render_handle) ; render_handle = NULL ;
   }
   FREE_VOLUMES ;

   /* accept this dataset */

   dset = qset ;

   npixels = 256 ;                             /* size of image to render */
   npixels = MAX( npixels , DSET_NX(dset) ) ;
   npixels = MAX( npixels , DSET_NY(dset) ) ;
   npixels = MAX( npixels , DSET_NZ(dset) ) ;

   /* refit the sub-brick selector menu */

   if( dset_ival >= DSET_NVALS(dset) ) dset_ival = DSET_NVALS(dset)-1 ;

   refit_MCW_optmenu( choose_av ,
                      0 ,                       /* new minval */
                      DSET_NVALS(dset)-1 ,      /* new maxval */
                      dset_ival ,               /* new inival */
                      0 ,                       /* new decim? */
                      REND_choose_av_label_CB , /* text routine */
                      dset                      /* text data */
                    ) ;

   AV_SENSITIZE( choose_av , (DSET_NVALS(dset) > 1) ) ;

   /* write the informational label */

   strcpy( dset_title , dsl[id].title ) ;
   fac = DSET_BRICK_FACTOR(dset,dset_ival) ;

   if( fac == 0.0 || fac == 1.0 ){
      strcpy(str,dset_title) ;
   } else {
      char abuf[16] ;
      AV_fval_to_char( fac , abuf ) ;
      sprintf(str,"%s [* %s]", dset_title , abuf ) ;
   }
   xstr = XmStringCreateLtoR( str , XmFONTLIST_DEFAULT_TAG ) ;
   XtVaSetValues( info_lab , XmNlabelString , xstr , NULL ) ;
   XmStringFree(xstr) ;

   /* if the existing overlay dataset doesn't match this one, kill the overlay */

   if( func_dset != NULL && ( DSET_NX(dset) != DSET_NX(func_dset) ||
                              DSET_NY(dset) != DSET_NY(func_dset) ||
                              DSET_NZ(dset) != DSET_NZ(func_dset)   ) ){

      INVALIDATE_OVERLAY ;
      func_dset = NULL ;

      TURNOFF_OVERLAY_WIDGETS ;

      (void) MCW_popup_message( choose_pb ,
                                   " \n"
                                   "** New underlay dataset did  **\n"
                                   "** not match dimensions of   **\n"
                                   "** existing overlay dataset, **\n"
                                   "** so the latter was removed **\n" ,
                                MCW_USER_KILL | MCW_TIMER_KILL ) ;
   }

   /* read the new data */

   new_dset = 1 ;           /* flag it as new */
   REND_reload_dataset() ;  /* load the data */

   return ;
}

void REND_finalize_func_CB( Widget w, XtPointer fd, MCW_choose_cbs * cbs )
{
   int id = cbs->ival ;
   THD_3dim_dataset * qset , * oset ;
   XmString xstr ;
   char str[2*THD_MAX_NAME] ;
   float fac ;

   /* check for errors */

   if( ! renderer_open ){ POPDOWN_strlist_chooser ; XBell(dc->display,100) ; return ; }

   if( id < 0 || id >= ndsl ){ XBell(dc->display,100) ; return ; }

   qset = PLUTO_find_dset( &(dsl[id].idcode) ) ;  /* the new dataset? */

   if( qset == NULL ){ XBell(dc->display,100) ; return ; }

   /* accept this dataset */

   oset      = func_dset ;
   func_dset = qset ;

   /* refit the sub-brick selector menus */

   if( oset == NULL ){ func_color_ival = 0 ; func_thresh_ival = 1 ; }

   if( func_color_ival >= DSET_NVALS(func_dset) )
      func_color_ival = DSET_NVALS(func_dset)-1;

   refit_MCW_optmenu( wfunc_color_av ,
                      0 ,                       /* new minval */
                      DSET_NVALS(func_dset)-1 , /* new maxval */
                      func_color_ival ,         /* new inival */
                      0 ,                       /* new decim? */
                      REND_choose_av_label_CB , /* text routine */
                      func_dset                      /* text data */
                    ) ;

   AV_SENSITIZE( wfunc_color_av , (DSET_NVALS(func_dset) > 1) ) ;

   if( func_thresh_ival >= DSET_NVALS(func_dset) )
      func_thresh_ival = DSET_NVALS(func_dset)-1;

   refit_MCW_optmenu( wfunc_thresh_av ,
                      0 ,                       /* new minval */
                      DSET_NVALS(func_dset)-1 , /* new maxval */
                      func_thresh_ival ,        /* new inival */
                      0 ,                       /* new decim? */
                      REND_choose_av_label_CB , /* text routine */
                      func_dset                      /* text data */
                    ) ;

   AV_SENSITIZE( wfunc_thresh_av , (DSET_NVALS(func_dset) > 1) ) ;

   /* write the informational label */

   strcpy( func_dset_title , dsl[id].title ) ;
   fac = DSET_BRICK_FACTOR(func_dset,func_color_ival) ;

   if( fac == 0.0 || fac == 1.0 ){
      strcpy(str,func_dset_title) ;
   } else {
      char abuf[16] ;
      AV_fval_to_char( fac , abuf ) ;
      sprintf(str,"%s [* %s]", func_dset_title , abuf ) ;
   }
   xstr = XmStringCreateLtoR( str , XmFONTLIST_DEFAULT_TAG ) ;
   XtVaSetValues( wfunc_info_lab , XmNlabelString , xstr , NULL ) ;
   XmStringFree(xstr) ;

   /* fix the range labels */

   xstr = REND_range_label() ;
   XtVaSetValues( wfunc_range_label , XmNlabelString , xstr , NULL ) ;
   XmStringFree(xstr) ;

   xstr = REND_autorange_label() ;
   XtVaSetValues( wfunc_range_bbox->wbut[0], XmNlabelString,xstr , NULL ) ;
   XmStringFree(xstr) ;

   /* fix the p-value label */

   REND_set_thr_pval() ;

   /* read the new data */

#if 0
   new_dset = 1 ;           /* flag it as new */
   REND_reload_dataset() ;  /* load the data */
#endif

   return ;
}

/*-------------------------------------------------------------------
  Callback for precalc menu
---------------------------------------------------------------------*/

void REND_precalc_CB( MCW_arrowval * av , XtPointer cd )
{
   precalc_ival = av->ival ;
   return ;
}

/*-------------------------------------------------------------------
  Callback for angle arrowvals
---------------------------------------------------------------------*/

void REND_angle_CB( MCW_arrowval * av , XtPointer cd )
{
   float na ;

   if( av == roll_av  ){

      na = angle_roll = av->fval ;
           if( na <    0.0 ) na += 360 ;
      else if( na >= 360.0 ) na -= 360.0 ;
      if( na != av->fval ){ AV_assign_fval( av , na ) ; angle_roll = na ; }

   } else if( av == pitch_av ){

     na = angle_pitch = av->fval ;
           if( na <    0.0 ) na += 360 ;
      else if( na >= 360.0 ) na -= 360.0 ;
      if( na != av->fval ){ AV_assign_fval( av , na ) ; angle_pitch = na ; }

   } else if( av == yaw_av   ){

      na = angle_yaw = av->fval ;
           if( na <    0.0 ) na += 360 ;
      else if( na >= 360.0 ) na -= 360.0 ;
      if( na != av->fval ){ AV_assign_fval( av , na ) ; angle_yaw = na ; }

   } else {
      return ;  /* should never happen */
   }

   if( dynamic_flag && render_handle != NULL ) REND_draw_CB(NULL,NULL,NULL) ;

   return ;
}

/*-----------------------------------------------------------------------
   Callback for xhair toggle button
-------------------------------------------------------------------------*/

#define CHECK_XHAIR_ERROR                                               \
  do{ if( xhair_flag && dset!=NULL &&                                   \
          ! EQUIV_DATAXES(dset->daxes,im3d->wod_daxes) ){               \
        MCW_set_bbox( xhair_bbox , 0 ) ; xhair_flag = 0 ;               \
        (void) MCW_popup_message( xhair_bbox->wrowcol ,                 \
                                     "Can't overlay AFNI crosshairs\n"  \
                                     "because dataset grid and AFNI\n"  \
                                     "viewing grid don't coincide."   , \
                                  MCW_USER_KILL | MCW_TIMER_KILL ) ;    \
        XBell(dc->display,100) ; return ;                               \
     } } while(0)

void REND_xhair_CB( Widget w , XtPointer client_data , XtPointer call_data )
{
   int old_xh = xhair_flag ;

   xhair_flag = MCW_val_bbox( xhair_bbox ) ;
   if( old_xh == xhair_flag ) return ;

   CHECK_XHAIR_ERROR ;
   FREE_VOLUMES ;

   xhair_ixold = -666 ; xhair_jyold = -666 ; xhair_kzold = -666 ; /* forget */

   if( dynamic_flag && render_handle != NULL ) REND_draw_CB(NULL,NULL,NULL) ;
   return ;
}

void REND_dynamic_CB( Widget w , XtPointer client_data , XtPointer call_data )
{
   dynamic_flag = MCW_val_bbox( dynamic_bbox ) ;
   return ;
}

void REND_accum_CB( Widget w , XtPointer client_data , XtPointer call_data )
{
   accum_flag = MCW_val_bbox( accum_bbox ) ;
   return ;
}

/*-----------------------------------------------------------------------
   Overlay some lines showing the crosshair location.
   Note that this function assumes that the current anat dataset
   in AFNI is defined on exactly the same grid as the rendering dataset.
-------------------------------------------------------------------------*/

#define GR(i,j,k) gar[(i)+(j)*nx+(k)*nxy]
#define OP(i,j,k) oar[(i)+(j)*nx+(k)*nxy]

#define GXH_GRAY  255
#define GXH_COLOR 127
#define OXH       255

void REND_xhair_overlay(void)
{
   int ix,jy,kz , nx,ny,nz,nxy , ii , gap ;
   byte * gar , * oar ;
   byte   gxh ,   oxh=OXH ;

   if( grim == NULL || opim == NULL ) return ;  /* error */

   gxh = (func_computed) ? GXH_COLOR : GXH_GRAY ;

   CHECK_XHAIR_ERROR ;

   ix = im3d->vinfo->i1 ; nx = grim->nx ;
   jy = im3d->vinfo->j2 ; ny = grim->ny ; nxy = nx * ny ;
   kz = im3d->vinfo->k3 ; nz = grim->nz ;

   if( ix < 0 || ix >= nx ) return ;  /* error */
   if( jy < 0 || jy >= ny ) return ;  /* error */
   if( kz < 0 || kz >= nz ) return ;  /* error */

   gap = im3d->vinfo->crosshair_gap ;
   gar = MRI_BYTE_PTR(grim) ;
   oar = MRI_BYTE_PTR(opim) ;

   for( ii=0 ; ii < nx ; ii++ ){
      if( abs(ii-ix) > gap ){ GR(ii,jy,kz) = gxh ; OP(ii,jy,kz) = oxh ; }
   }

   for( ii=0 ; ii < ny ; ii++ ){
      if( abs(ii-jy) > gap ){ GR(ix,ii,kz) = gxh ; OP(ix,ii,kz) = oxh ; }
   }

   for( ii=0 ; ii < nz ; ii++ ){
      if( abs(ii-kz) > gap ){ GR(ix,jy,ii) = gxh ; OP(ix,jy,ii) = oxh ; }
   }

   xhair_ixold = ix ; xhair_jyold = jy ; xhair_kzold = kz ;  /* memory */
   return ;
}

/*------------------------------------------------------------------
   Called when the user changes a graph - just signals that we
   need to reload the data.
--------------------------------------------------------------------*/

void REND_graf_CB( MCW_graf * gp , void * cd )
{
   FREE_VOLUMES ;  /* free the volumes, will force reloading at redraw */
   return ;
}

/*-------------------------------------------------------------------
  Callback for clipping arrowvals
---------------------------------------------------------------------*/

void REND_clip_CB( MCW_arrowval * av , XtPointer cd )
{
   FREE_VOLUMES ;  /* free the volumes, will force reloading */

   if( clipbot_av->ival >= cliptop_av->ival ){
      if( av == clipbot_av )
         AV_assign_ival( clipbot_av , cliptop_av->ival - 1 ) ;
      else
         AV_assign_ival( cliptop_av , clipbot_av->ival + 1 ) ;
   }

   /* if brick is scaled, re-show the scaled labels */

   if( brickfac != 0.0 && brickfac != 1.0 ){
      char minch[16] , maxch[16] , str[64] ;
      XmString xstr ;

      if( av == clipbot_av ){
         AV_fval_to_char( brickfac * clipbot_av->ival , minch ) ;
         sprintf(str,"[-> %s]",minch) ;
         xstr = XmStringCreateLtoR( str , XmFONTLIST_DEFAULT_TAG ) ;
         XtVaSetValues( clipbot_faclab , XmNlabelString , xstr , NULL ) ;
         XmStringFree(xstr) ;
      } else {
         AV_fval_to_char( brickfac * cliptop_av->ival , maxch ) ;
         sprintf(str,"[-> %s]",maxch) ;
         xstr = XmStringCreateLtoR( str , XmFONTLIST_DEFAULT_TAG ) ;
         XtVaSetValues( cliptop_faclab , XmNlabelString , xstr , NULL ) ;
         XmStringFree(xstr) ;
      }
   }

   return ;
}

/*-------------------------------------------------------------------
  Callback for cutout type optmenu
  -- change the appearance of the widgets that follow it
---------------------------------------------------------------------*/

void REND_cutout_type_CB( MCW_arrowval * av , XtPointer cd )
{
   int iv , val ;
   XmString xstr ;
   Boolean sens ;

   for( iv=0 ; iv < num_cutouts ; iv++ )
      if( av == cutouts[iv]->type_av ) break ;
   if( iv == num_cutouts ) return ;

   val = av->ival ;           /* new type */

   HIDE_SCALE ;

   if( val == CUT_NONE ){
      XtUnmanageChild( cutouts[iv]->param_lab ) ;
      XtUnmanageChild( cutouts[iv]->param_av->wrowcol ) ;
      XtUnmanageChild( cutouts[iv]->set_pb ) ;
      XtUnmanageChild( cutouts[iv]->mustdo_bbox->wrowcol ) ;
   } else {
      xstr = XmStringCreateLtoR( cutout_param_labels[val], XmFONTLIST_DEFAULT_TAG ) ;
      XtVaSetValues( cutouts[iv]->param_lab , XmNlabelString , xstr , NULL ) ;
      XmStringFree(xstr) ;

      XtManageChild( cutouts[iv]->param_lab ) ;
      XtManageChild( cutouts[iv]->param_av->wrowcol ) ;
      XtManageChild( cutouts[iv]->set_pb ) ;
      XtManageChild( cutouts[iv]->mustdo_bbox->wrowcol ) ;

#undef DESENS
#ifdef DESENS
      sens = (val != CUT_EXPRESSION) ;                    /* deactivate */
      XtSetSensitive(cutouts[iv]->param_av->wup  ,sens) ; /* if is an   */
      XtSetSensitive(cutouts[iv]->param_av->wdown,sens) ; /* Expr > 0   */
      XtSetSensitive(cutouts[iv]->set_pb         ,sens) ; /* cutout     */
#else
      if( val == CUT_EXPRESSION ){                        /* if Expr > 0 */
         XtUnmanageChild( cutouts[iv]->param_av->wup   ); /* expand the  */
         XtUnmanageChild( cutouts[iv]->param_av->wdown ); /* size of the */
         XtUnmanageChild( cutouts[iv]->set_pb          ); /* text field  */
         XtVaSetValues( cutouts[iv]->param_av->wtext ,
                           XmNcolumns , AV_NCOL + 9 ,
                        NULL ) ;
      } else {                                          /* shrink size   */
         XtVaSetValues( cutouts[iv]->param_av->wtext ,  /* of text field */
                           XmNcolumns , AV_NCOL ,       /* to normal for */
                        NULL ) ;                        /* other cutouts */
         XtManageChild( cutouts[iv]->param_av->wup   );
         XtManageChild( cutouts[iv]->param_av->wdown );
         XtManageChild( cutouts[iv]->set_pb          );
      }
#endif
   }

   FIX_SCALE_SIZE ;
   return ;
}

/*-------------------------------------------------------------------
  Callback for cutout number optmenu
---------------------------------------------------------------------*/

void REND_numcutout_CB( MCW_arrowval * av , XtPointer cd )
{
   int ii ;
   num_cutouts = av->ival ;

   HIDE_SCALE ;

   for( ii=0 ; ii < MAX_CUTOUTS ; ii++ ){
      if( ii < num_cutouts )
         XtManageChild( cutouts[ii]->hrc ) ;
      else
         XtUnmanageChild( cutouts[ii]->hrc ) ;
   }

   FIX_SCALE_SIZE ;
   return ;
}

/*---------------------------------------------------------------------
   Routines to load and compare cutout states
-----------------------------------------------------------------------*/

void REND_load_cutout_state(void)
{
   int ii ;
   char * str ;

   current_cutout_state.num   = num_cutouts ;
   current_cutout_state.logic = logic_cutout = logiccutout_av->ival ;

   for( ii=0 ; ii < MAX_CUTOUTS ; ii++ ){
      current_cutout_state.type[ii]   = cutouts[ii]->type_av->ival ;
      current_cutout_state.mustdo[ii] = MCW_val_bbox( cutouts[ii]->mustdo_bbox ) ;
      current_cutout_state.param[ii]  = REND_evaluate( cutouts[ii]->param_av ) ;

      if( current_cutout_state.type[ii] == CUT_EXPRESSION ){
         str = XmTextFieldGetString( cutouts[ii]->param_av->wtext ) ;
         strcpy( current_cutout_state.param_str[ii] , str ) ;
         XtFree(str) ;
      } else {
         current_cutout_state.param_str[ii][0] = '\0' ;
      }
   }

   current_cutout_state.opacity_scale = REND_evaluate( opacity_scale_av ) ;
   current_cutout_state.opacity_scale = MAX( MIN_OPACITY_SCALE ,
                                             current_cutout_state.opacity_scale ) ;
   current_cutout_state.opacity_scale = MIN( 1.000 ,
                                             current_cutout_state.opacity_scale ) ;
   return ;
}

int REND_cutout_state_changed(void)
{
   int ii ;

   if( current_cutout_state.num != old_cutout_state.num ) return 1 ;
   if( current_cutout_state.num == 0                    ) return 0 ;

   if( current_cutout_state.num > 1 &&
       (current_cutout_state.logic != old_cutout_state.logic) ) return 1 ;

   if( current_cutout_state.opacity_scale != old_cutout_state.opacity_scale ) return 1 ;

   for( ii=0 ; ii < current_cutout_state.num ; ii++ ){
      if( current_cutout_state.type[ii] != old_cutout_state.type[ii] ) return 1 ;

      if( current_cutout_state.type[ii] == CUT_NONE ) continue ;

      switch( current_cutout_state.type[ii] ){
         default :
          if( current_cutout_state.param[ii] != old_cutout_state.param[ii] ) return 1;
         break ;

         case CUT_EXPRESSION:
          if( strcmp( current_cutout_state.param_str[ii] ,
                      old_cutout_state.param_str[ii]      ) != 0 ) return 1 ;

          if( automate_flag &&
              strchr(current_cutout_state.param_str[ii],'t') != NULL ) return 1 ;
         break ;
      }

      if( current_cutout_state.logic != CUTOUT_OR &&
          current_cutout_state.num   >  1         &&
          current_cutout_state.mustdo[ii] != old_cutout_state.mustdo[ii] ) return 1 ;
   }

   return 0 ;
}

/*-------------------------------------------------------------------------------
   When the user presses a cutout "Get" button
---------------------------------------------------------------------------------*/

#define TT_XMID   0.0   /* centroid */
#define TT_YMID  16.0
#define TT_ZMID   5.0

#define TT_XSEMI 68.0   /* semi-axes */
#define TT_YSEMI 86.0
#define TT_ZSEMI 69.0

void REND_cutout_set_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   int iv , typ ;
   float val ;
   char str[16] ;

   for( iv=0 ; iv < num_cutouts ; iv++ )
      if( w == cutouts[iv]->set_pb ) break ;
   if( iv == num_cutouts ) return ;

   typ = cutouts[iv]->type_av->ival ;
   switch( typ ){

      default: return ;  /* error */

      case CUT_RIGHT_OF:
      case CUT_LEFT_OF:      val = im3d->vinfo->xi ; break ;

      case CUT_ANTERIOR_TO:
      case CUT_POSTERIOR_TO: val = im3d->vinfo->yj ; break ;

      case CUT_INFERIOR_TO:
      case CUT_SUPERIOR_TO:  val = im3d->vinfo->zk ; break ;

      case CUT_TT_ELLIPSOID:{
         float x = im3d->vinfo->xi , y = im3d->vinfo->yj , z = im3d->vinfo->zk  ;

         val =  (x-TT_XMID) * (x-TT_XMID) / (TT_XSEMI * TT_XSEMI)
              + (y-TT_YMID) * (y-TT_YMID) / (TT_YSEMI * TT_YSEMI)
              + (z-TT_ZMID) * (z-TT_ZMID) / (TT_ZSEMI * TT_ZSEMI) ;

         val = 0.1 * rint( 1000.0 * sqrt(val) ) ;  /* round to 1 decimal place */
      } break ;

      case CUT_SLANT_XPY_GT:
      case CUT_SLANT_XPY_LT:
      case CUT_SLANT_XMY_GT:
      case CUT_SLANT_XMY_LT:
      case CUT_SLANT_YPZ_GT:
      case CUT_SLANT_YPZ_LT:
      case CUT_SLANT_YMZ_GT:
      case CUT_SLANT_YMZ_LT:
      case CUT_SLANT_XPZ_GT:
      case CUT_SLANT_XPZ_LT:
      case CUT_SLANT_XMZ_GT:
      case CUT_SLANT_XMZ_LT:{
         float x = im3d->vinfo->xi , y = im3d->vinfo->yj , z = im3d->vinfo->zk  ;
         int isl = typ - CUT_SLANT_BASE ;

         val =   cut_slant_normals[isl][0] * x
               + cut_slant_normals[isl][1] * y
               + cut_slant_normals[isl][2] * z ;

         val = 0.1 * rint( 10.0 * val ) ;  /* round to 0.1 mm */
      }
      break ;
   }

   AV_assign_fval( cutouts[iv]->param_av , val ) ;

   if( dynamic_flag && render_handle != NULL ) REND_draw_CB(NULL,NULL,NULL) ;
   return ;
}

/*--------------------------------------------------------------------
   Actually do the cutouts in the opacity brick
----------------------------------------------------------------------*/

void REND_cutout_blobs(void)
{
   int ii,jj,kk , nx,ny,nz,nxy,nxyz , cc , typ , ncc,logic,nmust,nand,mus ;
   int ibot,itop , jbot,jtop , kbot,ktop ;
   float par ;
   float dx,dy,dz , xorg,yorg,zorg , xx,yy,zz ;
   byte * oar , * gar ;
   byte ncdone = 0 ;

   ncc   = current_cutout_state.num ;
   logic = current_cutout_state.logic ;
   if( ncc < 1 || opim == NULL ) return ;      /* error */

   /* find out if the logic is effectively "OR" */

   if( ncc == 1 ){
      logic = CUTOUT_OR ;
   } else {
      for( nmust=cc=0 ; cc < ncc ; cc++ )
         if( current_cutout_state.mustdo[cc] ) nmust++ ;
      if( nmust >= ncc-1 ) logic = CUTOUT_OR ;
   }

   /* initialize */

   oar = MRI_BYTE_PTR(opim) ; if( oar == NULL ) return ;
   nx  = opim->nx ;
   ny  = opim->ny ; nxy  = nx * ny ;
   nz  = opim->nz ; nxyz = nxy * nz ;

   if( logic == CUTOUT_AND ){
      gar = (byte *) malloc( sizeof(byte) * nxyz ) ;  /* counts of hits */
      memset( gar , 0 , sizeof(byte) * nxyz ) ;
   }

   dx   = dset->daxes->xxdel; dy   = dset->daxes->yydel; dz   = dset->daxes->zzdel;
   xorg = dset->daxes->xxorg; yorg = dset->daxes->yyorg; zorg = dset->daxes->zzorg;

   for( cc=0 ; cc < ncc ; cc++ ){              /* loop over cutouts */
      typ = current_cutout_state.type[cc] ;
      mus = current_cutout_state.mustdo[cc] ;
      par = current_cutout_state.param[cc] ;
      if( typ == CUT_NONE ) continue ;         /* error */

      switch( typ ){

         /*............................................................*/

         case CUT_RIGHT_OF:
         case CUT_LEFT_OF:
         case CUT_ANTERIOR_TO:
         case CUT_POSTERIOR_TO:
         case CUT_INFERIOR_TO:
         case CUT_SUPERIOR_TO:{         /* a rectangular region */
           int q ;

           ibot = 0 ; itop = nx-1 ;     /* everything */
           jbot = 0 ; jtop = ny-1 ;
           kbot = 0 ; ktop = nz-1 ;
           switch( typ ){
              case CUT_RIGHT_OF:
                 q = (int)( (par-xorg)/dx - 0.499 ); RANGE(q,0,itop); itop = q;
              break ;
              case CUT_LEFT_OF:
                 q = (int)( (par-xorg)/dx + 1.499 ); RANGE(q,0,itop); ibot = q;
              break ;
              case CUT_ANTERIOR_TO:
                 q = (int)( (par-yorg)/dy - 0.499 ); RANGE(q,0,jtop); jtop = q;
              break ;
              case CUT_POSTERIOR_TO:
                 q = (int)( (par-yorg)/dy + 1.499 ); RANGE(q,0,jtop); jbot = q;
              break ;
              case CUT_INFERIOR_TO:
                 q = (int)( (par-zorg)/dz - 0.499 ); RANGE(q,0,ktop); ktop = q;
              break ;
              case CUT_SUPERIOR_TO:
                 q = (int)( (par-zorg)/dz + 1.499 ); RANGE(q,0,ktop); kbot = q;
              break ;
           }

           if( logic == CUTOUT_AND && ! mus ){        /* count hits */
              ncdone++ ;
              for( kk=kbot ; kk <= ktop ; kk++ )
                 for( jj=jbot ; jj <= jtop ; jj++ )
                    for( ii=ibot ; ii <= itop ; ii++ ) GR(ii,jj,kk)++ ;

           } else {                                   /* just blast it */
              for( kk=kbot ; kk <= ktop ; kk++ )
                 for( jj=jbot ; jj <= jtop ; jj++ )
                    for( ii=ibot ; ii <= itop ; ii++ ) OP(ii,jj,kk) = 0 ;
           }
         }
         break ;  /* end of rectangular region cutout */

         /*............................................................*/

         case CUT_TT_ELLIPSOID:{                 /* an ellipsoid */

           float dxa=dx/TT_XSEMI  , dya=dy/TT_YSEMI  , dza=dz/TT_ZSEMI   ;
           float xga=(xorg-TT_XMID)/TT_XSEMI ,
                 yga=(yorg-TT_YMID)/TT_YSEMI ,
                 zga=(zorg-TT_ZMID)/TT_ZSEMI , ebot=0.0001*par*par ;

           if( logic == CUTOUT_AND && ! mus ){        /* count hits */
              ncdone++ ;
              for( kk=0 ; kk < nz ; kk++ ){
                zz = zga + kk*dza ; zz = zz*zz ;
                for( jj=0 ; jj < ny ; jj++ ){
                  yy = yga + jj*dya ; yy = yy*yy + zz ;
                  if( yy < ebot ){
                    for( ii=0 ; ii < nx ; ii++ ){
                      xx = xga + ii*dxa ; xx = xx*xx + yy ;
                      if( xx > ebot ) GR(ii,jj,kk)++ ;
                    }
                  } else {
                    for( ii=0 ; ii < nx ; ii++ ) GR(ii,jj,kk)++ ;
                  }
              }}

           } else {                                   /* blast it */
              for( kk=0 ; kk < nz ; kk++ ){
                zz = zga + kk*dza ; zz = zz*zz ;
                for( jj=0 ; jj < ny ; jj++ ){
                  yy = yga + jj*dya ; yy = yy*yy + zz ;
                  if( yy < ebot ){
                    for( ii=0 ; ii < nx ; ii++ ){
                      xx = xga + ii*dxa ; xx = xx*xx + yy ;
                      if( xx > ebot ) OP(ii,jj,kk) = 0 ;
                    }
                  } else {
                    for( ii=0 ; ii < nx ; ii++ ) OP(ii,jj,kk) = 0 ;
                  }
              }}
           }
         }
         break ;  /* end of ellipsoid cutout */

         /*............................................................*/

#define VSIZE nx
         case CUT_EXPRESSION:{      /* expression > 0 */
            PARSER_code * pcode ;
            double * abc[26] , * temp ;

            /* parse the expression */

            pcode = PARSER_generate_code( current_cutout_state.param_str[cc] ) ;
            if( pcode == NULL ) break ;  /* skip this cutout */

            /* create the evaluation workspaces */

            temp = (double *) malloc( sizeof(double) * VSIZE ) ;
            for( jj=0 ; jj < 26 ; jj++ )
               abc[jj] = (double *) malloc( sizeof(double) * VSIZE ) ;

            for( jj=0 ; jj < 23 ; jj++ )       /* load zeros for */
               for( ii=0 ; ii < VSIZE; ii++ )  /* 'a' ... 'w'    */
                  abc[jj][ii] = 0.0 ;

            for( ii=0 ; ii < VSIZE ; ii++ ){   /* load 't' and 'n' */
               abc[N_IND][ii] = atoz[N_IND] ;
               abc[T_IND][ii] = atoz[T_IND] ;
            }

            /* loop over rows of voxels and evaluate expressions */

            for( kk=0 ; kk < nz ; kk++ ){
              zz = zorg + kk*dz ;
              for( jj=0 ; jj < ny ; jj++ ){
                yy = yorg + jj*dy ;

                for( ii=0 ; ii < nx ; ii++ ){      /* load row */
                   abc[X_IND][ii] = xorg + ii*dx ;
                   abc[Y_IND][ii] = yy ;
                   abc[Z_IND][ii] = zz ;
                }

                /* evaluate the expression */

                PARSER_evaluate_vector(pcode, abc, VSIZE, temp);

                /* cut cut cut */

                if( logic == CUTOUT_AND && ! mus ){        /* count hits */
                   ncdone++ ;
                   for( ii=0 ; ii < nx ; ii++ )
                     if( temp[ii] > 0.0 ) GR(ii,jj,kk)++ ;

                } else {                                   /* blast'em */
                   for( ii=0 ; ii < nx ; ii++ )
                     if( temp[ii] > 0.0 ) OP(ii,jj,kk) = 0 ;
                }
            }} /* end of loops over jj,kk (y,z) */

            /* free workspaces */

            for( jj=0 ; jj < 26 ; jj++ ) free(abc[jj]) ;
            free(temp) ; free(pcode) ;
         }
         break ;  /* end of expression cutout */

         /*............................................................*/

         case CUT_SLANT_XPY_GT:
         case CUT_SLANT_XPY_LT:
         case CUT_SLANT_XMY_GT:
         case CUT_SLANT_XMY_LT:
         case CUT_SLANT_YPZ_GT:
         case CUT_SLANT_YPZ_LT:
         case CUT_SLANT_YMZ_GT:
         case CUT_SLANT_YMZ_LT:
         case CUT_SLANT_XPZ_GT:
         case CUT_SLANT_XPZ_LT:
         case CUT_SLANT_XMZ_GT:
         case CUT_SLANT_XMZ_LT:{
            int isl = typ - CUT_SLANT_BASE ;
            float xn = cut_slant_normals[isl][0] , dxn = dx * xn ,
                  yn = cut_slant_normals[isl][1] , dyn = dy * yn ,
                  zn = cut_slant_normals[isl][2] , dzn = dz * zn , pval ;

            pval = par - xn*xorg - yn*yorg - zn*zorg ;

            if( logic == CUTOUT_AND && ! mus ){        /* count hits */
              ncdone++ ;
              for( kk=0,zz=-pval ; kk < nz ; kk++,zz+=dzn ){
                /* zz = kk * dzn - pval ; */
                for( jj=0,yy=zz ; jj < ny ; jj++,yy+=dyn ){
                  /* yy = jj * dyn + zz ; */
                  for( ii=0,xx=yy ; ii < nx ; ii++,xx+=dxn ){
                    /* xx = ii * dxn + yy ; */
                    if( xx > 0.0 ) GR(ii,jj,kk)++ ;
                  }
              }}
            } else {                                   /* blast it */
              for( kk=0,zz=-pval ; kk < nz ; kk++,zz+=dzn ){
                /* zz = kk * dzn - pval ; */
                for( jj=0,yy=zz ; jj < ny ; jj++,yy+=dyn ){
                  /* yy = jj * dyn + zz ; */
                  for( ii=0,xx=yy ; ii < nx ; ii++,xx+=dxn ){
                    /* xx = ii * dxn + yy ; */
                    if( xx > 0.0 ) OP(ii,jj,kk) = 0 ;
                  }
              }}
            }
         }
         break ;  /* end of slant cutout */

      } /* end of switch over type of cutout */
   } /* end of loop over cutouts */

   /* with AND, blast only those that were hit every time */

   if( logic == CUTOUT_AND && ncdone > 0 ){
      for( ii=0 ; ii < nxyz ; ii++ ) if( gar[ii] == ncdone ) oar[ii] = 0 ;
      free(gar) ;
   }

   return ;
}

/*-----------------------------------------------------------------------
   Callback for cutout parameter arrowval
   -- only action is to redraw if dynamic mode is on
-------------------------------------------------------------------------*/

void REND_param_CB( MCW_arrowval * av , XtPointer cd )
{
   if( dynamic_flag && render_handle != NULL ) REND_draw_CB(NULL,NULL,NULL) ;
}

/*-----------------------------------------------------------------------
   Evaluate an arrowval string that may be a number or may be
   a more complicated arithmetic expression.  Input variables are
   stored in the global array atoz[] ;
-------------------------------------------------------------------------*/

float REND_evaluate( MCW_arrowval * av )
{
   PARSER_code * pcode ;
   char * str , * cpt ;
   float val ;

   /* get the string to convert */

   if( av        == NULL ) return 0.0 ;        /* these cases should */
   if( av->wtext == NULL ) return av->fval ;   /* never happen       */

   str = XmTextFieldGetString( av->wtext ) ;
   if( str == NULL || str[0] == '\0' ){ XtFree(str) ; return 0.0 ; }

   /* try a regular numerical conversion */

   val = strtod( str , &cpt ) ;

   if( *cpt == '\0' || *cpt == ' ' ){ XtFree(str); AV_assign_fval(av,val); return val; }

   /* try to parse an expression */

   pcode = PARSER_generate_code( str ) ;
   if( pcode == NULL ){ XtFree(str) ; return 0.0 ; }

   val = PARSER_evaluate_one( pcode , atoz ) ; free(pcode) ;

   XtFree(str) ; return val ;
}

/*-------------------------------------------------------------------------
   When the user presses Enter in a textfield that might have an
   expression entered, evaluate the expression numerically and
   load that number back into the textfield.
---------------------------------------------------------------------------*/

void REND_textact_CB( Widget wtex, XtPointer client_data, XtPointer call_data )
{
   MCW_arrowval * av         = (MCW_arrowval *) client_data ;
   XmAnyCallbackStruct * cbs = (XmAnyCallbackStruct *) call_data ;
   float sval ;
   int iv ;

   for( iv=0 ; iv < num_cutouts ; iv++ )  /* skip if is an Expr > 0 cutout */
      if( av == cutouts[iv]->param_av &&
          cutouts[iv]->type_av->ival == CUT_EXPRESSION ) return ;

   MCW_invert_widget(wtex) ;

   sval = REND_evaluate( av ) ;
   AV_assign_fval( av , sval ) ;

   MCW_invert_widget(wtex) ;
   return ;
}

/**********************************************************************
   Stuff to deal with the image display window
***********************************************************************/

/*-----------------------------------------------------------------------
   Open an image display window.
-------------------------------------------------------------------------*/

static int any_rgb_images ;

void REND_open_imseq( void )
{
   int ntot , ii ;

   if( imseq != NULL      ||
       renderings == NULL || IMARR_COUNT(renderings) == 0 ) return ;

   ntot = IMARR_COUNT(renderings) ;

   any_rgb_images = 0 ;
   for( ii=0 ; ii < ntot ; ii++ ){
      if( IMARR_SUBIMAGE(renderings,ii) != NULL &&
          IMARR_SUBIMAGE(renderings,ii)->kind == MRI_rgb ){

         any_rgb_images = 1 ; break ;
      }
   }

   imseq = open_MCW_imseq( dc , REND_imseq_getim , NULL ) ;

   drive_MCW_imseq( imseq , isqDR_clearstat , NULL ) ;

   { ISQ_options opt ;       /* change some options from the defaults */

     ISQ_DEFAULT_OPT(opt) ;
     opt.save_one = False ;  /* change to Save:bkg */
     opt.save_pnm = False ;
     drive_MCW_imseq( imseq , isqDR_options      , (XtPointer) &opt ) ;
     drive_MCW_imseq( imseq , isqDR_periodicmont , (XtPointer) 0    ) ;
   }

   /* make it popup */

   drive_MCW_imseq( imseq , isqDR_realize, NULL ) ;

   drive_MCW_imseq( imseq , isqDR_title, "AFNI Renderings" ) ;

   if( ntot == 1 )
      drive_MCW_imseq( imseq , isqDR_onoffwid , (XtPointer) isqDR_offwid ) ;
   else
      drive_MCW_imseq( imseq , isqDR_onoffwid , (XtPointer) isqDR_onwid ) ;

   drive_MCW_imseq( imseq , isqDR_reimage , (XtPointer) (ntot-1) ) ;

#ifndef DONT_INSTALL_ICONS
   if( afni48_good )
         drive_MCW_imseq( imseq,isqDR_icon , (XtPointer) afni48_pixmap ) ;
#endif

   return ;
}

void REND_update_imseq( void )
{
   int ntot , ii ;

   if( imseq == NULL ){ REND_open_imseq() ; return ; }
   if( renderings == NULL || IMARR_COUNT(renderings) == 0 ) return ;

   ntot = IMARR_COUNT(renderings) ;

   any_rgb_images = 0 ;
   for( ii=0 ; ii < ntot ; ii++ ){
      if( IMARR_SUBIMAGE(renderings,ii) != NULL &&
          IMARR_SUBIMAGE(renderings,ii)->kind == MRI_rgb ){

         any_rgb_images = 1 ; break ;
      }
   }

   drive_MCW_imseq( imseq , isqDR_newseq , NULL ) ;

   if( ntot == 1 )
      drive_MCW_imseq( imseq , isqDR_onoffwid , (XtPointer) isqDR_offwid ) ;
   else
      drive_MCW_imseq( imseq , isqDR_onoffwid , (XtPointer) isqDR_onwid ) ;

   drive_MCW_imseq( imseq , isqDR_reimage , (XtPointer)(ntot-1) ) ;

   return ;
}

void REND_destroy_imseq( void )
{
   if( imseq == NULL ) return ;
   drive_MCW_imseq( imseq , isqDR_destroy , NULL ) ;
   return ;
}

/*------------------------------------------------------------------
   Routine to provide data to the imseq.
   Just returns the control information, or the selected image.
--------------------------------------------------------------------*/

XtPointer REND_imseq_getim( int n , int type , XtPointer handle )
{
   int ntot = 0 ;

   if( renderings != NULL ) ntot = IMARR_COUNT(renderings) ;
   if( ntot < 1 ) ntot = 1 ;

   /*--- send control info ---*/

   if( type == isqCR_getstatus ){
      MCW_imseq_status * stat = myXtNew( MCW_imseq_status ) ;  /* will be free-d */
                                                               /* when imseq is */
                                                               /* destroyed    */
      stat->num_total  = ntot ;
      stat->num_series = stat->num_total ;
      stat->send_CB    = REND_seq_send_CB ;
      stat->parent     = NULL ;
      stat->aux        = NULL ;

      stat->transforms0D = &(GLOBAL_library.registered_0D) ;
      stat->transforms2D = &(GLOBAL_library.registered_2D) ;

      return (XtPointer) stat ;
   }

   /*--- no overlay, never ---*/

   if( type == isqCR_getoverlay ) return NULL ;

   /*--- return a copy of a rendered image
         (since the imseq will delete it when it is done) ---*/

   if( type == isqCR_getimage || type == isqCR_getqimage ){
      MRI_IMAGE * im = NULL , * rim ;

      if( renderings != NULL ){
         if( n < 0 ) n = 0 ; else if( n >= ntot ) n = ntot-1 ;
         rim = IMARR_SUBIMAGE(renderings,n) ;
         if( any_rgb_images )
            im = mri_to_rgb( rim ) ;
         else
            im = mri_to_mri( rim->kind , rim ) ;
      }
      return (XtPointer) im ;
   }

   return NULL ; /* should not occur, but who knows? */
}

/*---------------------------------------------------------------------------
   Routine called when the imseq wants to send a message.
   In this case, all we need to handle is the destroy message,
   so that we can free some memory.
-----------------------------------------------------------------------------*/

void REND_seq_send_CB( MCW_imseq * seq , XtPointer handle , ISQ_cbs * cbs )
{
   switch( cbs->reason ){
      case isqCR_destroy:{
         myXtFree(imseq) ; imseq = NULL ;
      }
      break ;
   }
   return ;
}

/*----------------------------------------------------------------------------
   Automation callbacks
------------------------------------------------------------------------------*/

void REND_autoflag_CB( Widget w , XtPointer client_data , XtPointer call_data )
{
   int flag = MCW_val_bbox( automate_bbox ) ;
   XtSetSensitive( autocompute_pb , (Boolean) flag ) ;
   return ;
}

/*--------------------------------------------------------------------------
   Drive the automatic computation
----------------------------------------------------------------------------*/

static int autokill ;

void REND_autocompute_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   int it , ntime = autoframe_av->ival ;
   float scl = 100.0/ntime ;
   Widget autometer ;

   automate_flag = 1 ;
   if( ! accum_flag ){ DESTROY_IMARR(renderings) ; }

   atoz[N_IND] = ntime ;

   autometer = MCW_popup_meter( shell , METER_TOP_WIDE ) ;

   XtManageChild( autocancel_pb ) ; AFNI_add_interruptable( autocancel_pb ) ;
   autokill = 0 ;

   for( it=0 ; it < ntime ; it++ ){
      atoz[T_IND] = it ;
      AV_assign_ival( autoframe_av , it+1 ) ;

      REND_draw_CB(NULL,NULL,NULL) ;

      if( it < ntime-1 ){
         AFNI_process_interrupts(autocancel_pb) ;
         if( autokill ) break ;
      }

      MCW_set_meter( autometer , (int)(scl*(it+1)) ) ;
   }

   MCW_popdown_meter( autometer ) ;

   /*-- done: turn off automation --*/

   MCW_set_bbox( automate_bbox , 0 ) ;
   XtSetSensitive( autocompute_pb , False ) ;

   XtUnmanageChild( autocancel_pb ) ; AFNI_add_interruptable(NULL) ;

   automate_flag = 0 ;
   return ;
}

/*--------------------------------------------------------------------------
   Set a flag to cancel the automatic computation
----------------------------------------------------------------------------*/

void REND_autocancel_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   if( autokill ){ XBell(dc->display,100) ; return ; }
   autokill = 1 ;
}

/*--------------------------------------------------------------------------
   What happens when the user selects a sub-brick from a menu
----------------------------------------------------------------------------*/

void REND_choose_av_CB( MCW_arrowval * av , XtPointer cd )
{
   XmString xstr ;
   char str[2*THD_MAX_NAME] ;

   /*--- selection of a underlay sub-brick ---*/

   if( av == choose_av && dset != NULL && av->ival < DSET_NVALS(dset) ){

      float fac = DSET_BRICK_FACTOR(dset,av->ival) ;

      if( fac == 0.0 || fac == 1.0 ){  /* rewrite the informational label */
         strcpy(str,dset_title) ;
      } else {
         char abuf[16] ;
         AV_fval_to_char( fac , abuf ) ;
         sprintf(str,"%s [* %s]", dset_title , abuf ) ;
      }
      xstr = XmStringCreateLtoR( str , XmFONTLIST_DEFAULT_TAG ) ;
      XtVaSetValues( info_lab , XmNlabelString , xstr , NULL ) ;
      XmStringFree(xstr) ;

      dset_ival = av->ival ;   /* read this sub-brick */
      new_dset = 1 ;           /* flag it as new      */
      REND_reload_dataset() ;  /* load the data       */

   /*--- selection of overlay color sub-brick ---*/

   } else if( av == wfunc_color_av && func_dset != NULL && av->ival < DSET_NVALS(func_dset) ){

      float fac = DSET_BRICK_FACTOR(func_dset,av->ival) ;

      if( fac == 0.0 || fac == 1.0 ){  /* rewrite the informational label */
         strcpy(str,func_dset_title) ;
      } else {
         char abuf[16] ;
         AV_fval_to_char( fac , abuf ) ;
         sprintf(str,"%s [* %s]", func_dset_title , abuf ) ;
      }
      xstr = XmStringCreateLtoR( str , XmFONTLIST_DEFAULT_TAG ) ;
      XtVaSetValues( wfunc_info_lab , XmNlabelString , xstr , NULL ) ;
      XmStringFree(xstr) ;

      func_color_ival = av->ival ;

      /* fix the range labels */

      xstr = REND_range_label() ;
      XtVaSetValues( wfunc_range_label , XmNlabelString , xstr , NULL ) ;
      XmStringFree(xstr) ;

      xstr = REND_autorange_label() ;
      XtVaSetValues( wfunc_range_bbox->wbut[0], XmNlabelString,xstr , NULL ) ;
      XmStringFree(xstr) ;

      INVALIDATE_OVERLAY ;

   /*--- selection of overlay threshold sub-brick ---*/

   } else if( av == wfunc_thresh_av && func_dset != NULL && av->ival < DSET_NVALS(func_dset) ){

      func_thresh_ival = av->ival ;

      /* fix the range label */

      xstr = REND_range_label() ;
      XtVaSetValues( wfunc_range_label , XmNlabelString , xstr , NULL ) ;
      XmStringFree(xstr) ;

      /* fix the p-value label */

      REND_set_thr_pval() ;

      INVALIDATE_OVERLAY ;
   }

   return ;
}

/*---------------------------------------------------------------------------
   Make a label for a sub-brick selector menu
-----------------------------------------------------------------------------*/

char * REND_choose_av_label_CB( MCW_arrowval * av , XtPointer cd )
{
   static char blab[32] ;
   THD_3dim_dataset * dset = (THD_3dim_dataset *) cd ;
   static char * lfmt[3] = { "#%1d %-14.14s" , "#%2d %-14.14s" , "#%3d %-14.14s"  } ;
   static char * rfmt[3] = { "%-14.14s #%1d" , "%-14.14s #%2d" , "%-14.14s #%3d"  } ;

   if( ISVALID_3DIM_DATASET(dset) ){

#ifdef USE_RIGHT_BUCK_LABELS
      if( DSET_NVALS(dset) < 10 )
        sprintf(blab, rfmt[0] , DSET_BRICK_LABEL(dset,av->ival) , av->ival ) ;
      else if( DSET_NVALS(dset) < 100 )
        sprintf(blab, rfmt[1] , DSET_BRICK_LABEL(dset,av->ival) , av->ival ) ;
      else
        sprintf(blab, rfmt[2] , DSET_BRICK_LABEL(dset,av->ival) , av->ival ) ;
#else
      if( DSET_NVALS(dset) < 10 )
        sprintf(blab, lfmt[0] , av->ival , DSET_BRICK_LABEL(dset,av->ival) ) ;
      else if( DSET_NVALS(dset) < 100 )
        sprintf(blab, lfmt[1] , av->ival , DSET_BRICK_LABEL(dset,av->ival) ) ;
      else
        sprintf(blab, lfmt[2] , av->ival , DSET_BRICK_LABEL(dset,av->ival) ) ;
#endif
   }
   else
      sprintf(blab," #%d ",av->ival) ;  /* should not happen! */

   return blab ;
}

/*----------------------------------------------------------------------------------
   Make the widgets for the functional overlay
------------------------------------------------------------------------------------*/

void REND_func_widgets(void)
{
   XmString xstr ;
   Widget   wqqq ;
   int      sel_height ;

   /* top level managers */

   wfunc_vsep = SEP_VER(top_rowcol) ;

   wfunc_frame = XtVaCreateWidget(
                   "AFNI" , xmFrameWidgetClass , top_rowcol ,
                      XmNshadowType , XmSHADOW_ETCHED_IN ,
                      XmNshadowThickness , 5 ,
                      XmNtraversalOn , False ,
                      XmNinitialResourcesPersistent , False ,
                   NULL ) ;

   wfunc_uber_rowcol = XtVaCreateWidget(
                    "AFNI" , xmRowColumnWidgetClass , wfunc_frame ,
                       XmNorientation , XmVERTICAL ,
                       XmNpacking , XmPACK_TIGHT ,
                       XmNtraversalOn , False ,
                       XmNinitialResourcesPersistent , False ,
                    NULL ) ;

   xstr = XmStringCreateLtoR( NO_DATASET_STRING ,
                              XmFONTLIST_DEFAULT_TAG ) ;
   wfunc_info_lab = XtVaCreateManagedWidget(
                      "AFNI" , xmLabelWidgetClass , wfunc_uber_rowcol ,
                         XmNlabelString , xstr ,
                         XmNrecomputeSize , False ,
                         XmNinitialResourcesPersistent , False ,
                      NULL ) ;
   XmStringFree(xstr) ;

   SEP_HOR(wfunc_uber_rowcol) ;

   xstr = XmStringCreateLtoR( "Choose Overlay Dataset" , XmFONTLIST_DEFAULT_TAG ) ;
   wfunc_choose_pb = XtVaCreateManagedWidget(
                  "AFNI" , xmPushButtonWidgetClass , wfunc_uber_rowcol ,
                     XmNalignment   , XmALIGNMENT_CENTER ,
                     XmNlabelString , xstr ,
                     XmNtraversalOn , False ,
                     XmNinitialResourcesPersistent , False ,
                  NULL ) ;
   XmStringFree(xstr) ;
   XtAddCallback( wfunc_choose_pb, XmNactivateCallback, REND_choose_CB, NULL ) ;

   SEP_HOR(wfunc_uber_rowcol) ;

   wfunc_rowcol = XtVaCreateWidget(
                    "AFNI" , xmRowColumnWidgetClass , wfunc_uber_rowcol ,
                       XmNorientation , XmHORIZONTAL ,
                       XmNpacking , XmPACK_TIGHT ,
                       XmNtraversalOn , False ,
                       XmNinitialResourcesPersistent , False ,
                    NULL ) ;

   /*---------------------------- 1st column: threshold stuff ----------------------------*/

   wfunc_thr_rowcol = XtVaCreateWidget(
                        "AFNI" , xmRowColumnWidgetClass , wfunc_rowcol ,
                           XmNorientation , XmVERTICAL ,
                           XmNpacking , XmPACK_TIGHT ,
                           XmNmarginHeight, 0 ,
                           XmNmarginWidth , 0 ,
                           XmNtraversalOn , False ,
                           XmNinitialResourcesPersistent , False ,
                        NULL ) ;

   xstr = XmStringCreateLtoR( "Thresh" , XmFONTLIST_DEFAULT_TAG ) ;
   wfunc_thr_label = XtVaCreateManagedWidget(
                       "AFNI" , xmLabelWidgetClass , wfunc_thr_rowcol ,
                          XmNlabelString , xstr ,
                          XmNrecomputeSize , False ,
                          XmNinitialResourcesPersistent , False ,
                       NULL ) ;
   XmStringFree(xstr) ;

 { int smax , stop , decim , sstep ;                  /* 30 Nov 1997:       */
   decim = THR_TOP_EXPON ;                            /* compute parameters */
   smax  = (int)( pow(10.0,decim) + 0.001 ) ;         /* for scale display. */
   stop  = smax - 1 ;
   sstep = smax / 1000 ;  if( sstep < 1 ) sstep = 1 ;

#ifdef BOXUP_SCALE
   wqqq = XtVaCreateManagedWidget(
           "AFNI" , xmFrameWidgetClass , wfunc_thr_rowcol ,
            XmNshadowType , XmSHADOW_ETCHED_IN ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
          NULL ) ;
#else
   wqqq = wfunc_thr_rowcol ;
#endif

#if 1
   MCW_widget_geom( anat_frame , &sel_height , NULL,NULL,NULL ) ;
   sel_height -= (74 + 24*MAX_CUTOUTS) ;  /* shorter allows for widgets below */
#else
   sel_height = 290 ;                     /* a hardwired approach */
#endif

   wfunc_thr_scale =
      XtVaCreateManagedWidget(
         "scale" , xmScaleWidgetClass , wqqq ,
            XmNminimum , 0 ,
            XmNmaximum , stop ,
            XmNscaleMultiple , sstep ,
            XmNdecimalPoints , decim ,
            XmNshowValue , True ,
            XmNvalue , (int)(smax*func_threshold) ,
            XmNorientation , XmVERTICAL ,
            XmNheight , sel_height ,
            XmNborderWidth , 0 ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;
  }

#ifdef FIX_SCALE_SIZE_PROBLEM
   XtVaSetValues( wfunc_thr_scale , XmNuserData , (XtPointer) sel_height , NULL ) ;
#endif

   XtAddCallback( wfunc_thr_scale , XmNvalueChangedCallback ,
                  REND_thr_scale_CB , NULL ) ;

   XtAddCallback( wfunc_thr_scale , XmNdragCallback ,
                  REND_thr_scale_drag_CB , NULL ) ;

   /** label for computed p-value, under scale **/

   xstr = XmStringCreateLtoR( THR_PVAL_LABEL_NONE , XmFONTLIST_DEFAULT_TAG ) ;
   wfunc_thr_pval_label =
      XtVaCreateManagedWidget(
         "AFNI" , xmLabelWidgetClass , wfunc_thr_rowcol ,
            XmNlabelString , xstr ,
            XmNrecomputeSize , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;
   XmStringFree(xstr) ;

   /** optmenu to choose top value for scale **/

   wfunc_thr_top_av = new_MCW_arrowval( wfunc_thr_rowcol ,
                                        "**" ,
                                        MCW_AV_optmenu ,
                                        0,THR_TOP_EXPON,0 ,
                                        MCW_AV_notext , 0 ,
                                        REND_thresh_top_CB , NULL ,
                                        REND_thresh_tlabel_CB , NULL ) ;
   XtManageChild(wfunc_thr_rowcol) ;

   /*--------------- column 2: color chooser stuff ------------------------------*/

   wfunc_color_rowcol =
      XtVaCreateWidget(
         "AFNI" , xmRowColumnWidgetClass , wfunc_rowcol ,
            XmNorientation , XmVERTICAL ,
            XmNmarginHeight, 0 ,
            XmNmarginWidth , 0 ,
            XmNpacking , XmPACK_TIGHT ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;

   xstr = XmStringCreateLtoR( "Color" , XmFONTLIST_DEFAULT_TAG ) ;
   wfunc_color_label =
      XtVaCreateManagedWidget(
         "AFNI" , xmLabelWidgetClass , wfunc_color_rowcol ,
            XmNlabelString , xstr ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;
   XmStringFree(xstr) ;

   /**-- Popup menu to control some facets of the pbar --**/

   wfunc_pbar_menu = XmCreatePopupMenu( wfunc_color_label , "menu" , NULL , 0 ) ;

   XtInsertEventHandler( wfunc_color_label ,     /* handle events in label */

                               0
                             | ButtonPressMask   /* button presses */
                            ,
                            FALSE ,              /* nonmaskable events? */
                            REND_pbarmenu_EV ,   /* handler */
                            NULL ,               /* client data */
                            XtListTail           /* last in queue */
                          ) ;

   (void) XtVaCreateManagedWidget(
            "dialog" , xmLabelWidgetClass , wfunc_pbar_menu ,
               LABEL_ARG("--- Cancel ---") ,
               XmNrecomputeSize , False ,
               XmNinitialResourcesPersistent , False ,
            NULL ) ;

   (void) XtVaCreateManagedWidget(
            "dialog" , xmSeparatorWidgetClass , wfunc_pbar_menu ,
             XmNseparatorType , XmSINGLE_LINE , NULL ) ;

   wfunc_pbar_equalize_pb =
      XtVaCreateManagedWidget(
         "dialog" , xmPushButtonWidgetClass , wfunc_pbar_menu ,
            LABEL_ARG("Equalize Spacing") ,
            XmNmarginHeight , 0 ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;

   XtAddCallback( wfunc_pbar_equalize_pb , XmNactivateCallback ,
                  REND_pbarmenu_CB , im3d ) ;

   wfunc_pbar_settop_pb =
      XtVaCreateManagedWidget(
         "dialog" , xmPushButtonWidgetClass , wfunc_pbar_menu ,
            LABEL_ARG("Set Top Value") ,
            XmNmarginHeight , 0 ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;

   XtAddCallback( wfunc_pbar_settop_pb , XmNactivateCallback ,
                  REND_pbarmenu_CB , im3d ) ;

   (void) XtVaCreateManagedWidget(
            "dialog" , xmSeparatorWidgetClass , wfunc_pbar_menu ,
             XmNseparatorType , XmSINGLE_LINE , NULL ) ;

 { char * pb_dum_label[2] = { "Dummy" , "Dummy" } ;
   wfunc_pbar_palette_av = new_MCW_arrowval(
                             wfunc_pbar_menu ,     /* parent Widget */
                             "Set Pal " ,          /* label */
                             MCW_AV_optmenu ,      /* option menu style */
                             0 ,                   /* first option */
                             1 ,                   /* last option */
                             0 ,                   /* initial selection */
                             MCW_AV_readtext ,     /* ignored but needed */
                             0 ,                   /* ditto */
                             REND_palette_av_CB ,  /* callback when changed */
                             NULL ,                /* data for above */
                             MCW_av_substring_CB , /* text creation routine */
                             pb_dum_label          /* data for above */
                           ) ;
   }

   if( GPT != NULL && PALTAB_NUM(GPT) > 0 ){
      refit_MCW_optmenu( wfunc_pbar_palette_av ,
                           0 ,                     /* new minval */
                           PALTAB_NUM(GPT)-1 ,     /* new maxval */
                           0 ,                     /* new inival */
                           0 ,                     /* new decim? */
                           AFNI_palette_label_CB , /* text routine */
                           NULL                    /* text data */
                        ) ;
   } else {
      XtUnmanageChild( wfunc_pbar_palette_av->wrowcol ) ;
   }

   /**-- Color pbar to control intensity-to-color mapping --**/

 { float pmin=-1.0 , pmax=1.0 ;
   int npane = INIT_panes_sgn ;  /* from afni.h */

   sel_height -= 22 ;            /* a little shorter than the scale */

   wfunc_color_pbar = new_MCW_pbar(
                        wfunc_color_rowcol ,        /* parent */
                        dc ,                        /* display */
                        npane ,                     /* number panes */
                        sel_height / npane ,        /* init pane height */
                        pmin , pmax ,               /* value range */
                        REND_color_pbar_CB ,        /* callback */
                        NULL                ) ;     /* callback data */

   wfunc_color_pbar->parent       = NULL ;
   wfunc_color_pbar->mode         = 0 ;
   wfunc_color_pbar->npan_save[0] = INIT_panes_sgn ;  /* from afni.h */
   wfunc_color_pbar->npan_save[1] = INIT_panes_pos ;
   wfunc_color_pbar->hide_changes = INIT_panes_hide ;

   REND_setup_color_pbar() ;  /* other setup stuff */

   (void) XtVaCreateManagedWidget(
            "AFNI" , xmSeparatorWidgetClass , wfunc_color_rowcol ,
                XmNseparatorType , XmSINGLE_LINE ,
            NULL ) ;

   wfunc_colornum_av = new_MCW_arrowval(
                       wfunc_color_rowcol ,
                        "#" ,
                        MCW_AV_optmenu ,
                        NPANE_MIN , NPANE_MAX , npane ,
                        MCW_AV_notext , 0 ,
                        REND_colornum_av_CB , NULL ,
                        NULL,NULL ) ;

   if( NPANE_MAX >= COLSIZE )
      AVOPT_columnize( wfunc_colornum_av , 1+(NPANE_MAX+1)/COLSIZE ) ;
  }

   /*--- toggle button to control posfunc option for pbar ---*/

 { char * color_bbox_label[1] = { "Pos?" } ;
   wfunc_color_bbox = new_MCW_bbox( wfunc_color_rowcol ,
                                      1 , color_bbox_label ,
                                      MCW_BB_check ,
                                      MCW_BB_noframe ,
                                      REND_color_bbox_CB , NULL ) ;
 }

   XtManageChild(wfunc_color_rowcol) ;

   /*---------- Column 3:  Choices controls -------------------------------*/

   wfunc_choices_rowcol =
      XtVaCreateWidget(
         "AFNI" , xmRowColumnWidgetClass , wfunc_rowcol ,
            XmNorientation , XmVERTICAL ,
            XmNpacking , XmPACK_TIGHT ,
            XmNmarginHeight, 0 ,
            XmNmarginWidth , 0 ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;

   xstr = XmStringCreateLtoR( "Choices" , XmFONTLIST_DEFAULT_TAG ) ;
   wfunc_choices_label =
      XtVaCreateManagedWidget(
         "AFNI" , xmLabelWidgetClass , wfunc_choices_rowcol ,
            XmNlabelString , xstr ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;
   XmStringFree(xstr) ;

   /*--- 30 Nov 1997: sub-brick menus ---*/

   wfunc_buck_frame =
      XtVaCreateWidget(
         "AFNI" , xmFrameWidgetClass , wfunc_choices_rowcol ,
            XmNshadowType , XmSHADOW_ETCHED_IN ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;

   wfunc_buck_rowcol =
      XtVaCreateWidget(
         "AFNI" , xmRowColumnWidgetClass , wfunc_buck_frame ,
            XmNorientation , XmVERTICAL ,
            XmNpacking , XmPACK_TIGHT ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;

   /*--- Sub-brick selectors for Color & Threshold ---*/
   /*    (Actual labels are set when used)            */

   wfunc_color_av = new_MCW_arrowval(
                          wfunc_buck_rowcol    ,  /* parent Widget */
                          "Color" ,               /* label */
                          MCW_AV_optmenu ,        /* option menu style */
                          0 ,                     /* first option */
                          1 ,                     /* last option */
                          0 ,                     /* initial selection */
                          MCW_AV_readtext ,       /* ignored but needed */
                          0 ,                     /* decimal shift */
                          REND_choose_av_CB ,     /* callback when changed */
                          NULL ,                  /* data for above */
                          MCW_av_substring_CB ,   /* text creation routine */
                          REND_dummy_av_label     /* data for above */
                        ) ;

   wfunc_thresh_av = new_MCW_arrowval(
                          wfunc_buck_rowcol    ,  /* parent Widget */
                          "Thr  " ,               /* label */
                          MCW_AV_optmenu ,        /* option menu style */
                          0 ,                     /* first option */
                          1 ,                     /* last option */
                          0 ,                     /* initial selection */
                          MCW_AV_readtext ,       /* ignored but needed */
                          0 ,                     /* decimal shift */
                          REND_choose_av_CB ,     /* callback when changed */
                          NULL ,                  /* data for above */
                          MCW_av_substring_CB ,   /* text creation routine */
                          REND_dummy_av_label     /* data for above */
                        ) ;

   XtManageChild( wfunc_buck_rowcol ) ;
   XtManageChild( wfunc_buck_frame ) ;

   AV_SENSITIZE( wfunc_color_av  , False ) ;  /* turn these off for now */
   AV_SENSITIZE( wfunc_thresh_av , False ) ;

   /*--- menus to control opacity of color ---*/

   wfunc_opacity_frame =
      XtVaCreateWidget(
         "AFNI" , xmFrameWidgetClass , wfunc_choices_rowcol ,
            XmNshadowType , XmSHADOW_ETCHED_IN ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;

   wfunc_opacity_rowcol =
      XtVaCreateWidget(
         "AFNI" , xmRowColumnWidgetClass , wfunc_opacity_frame ,
            XmNorientation , XmVERTICAL ,
            XmNpacking , XmPACK_TIGHT ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;

 { static char * func_opacity_labels[11] = {
                       "Underlay" ,
                       " 0.1" , " 0.2" , " 0.3" , " 0.4" , " 0.5" ,
                       " 0.6" , " 0.7" , " 0.8" , " 0.9" , " 1.0"  } ;

   wfunc_opacity_av = new_MCW_arrowval(
                          wfunc_opacity_rowcol  , /* parent Widget */
                          "Color Opacity " ,      /* label */
                          MCW_AV_optmenu ,        /* option menu style */
                          0 ,                     /* first option */
                          10 ,                    /* last option */
                          5 ,                     /* initial selection */
                          MCW_AV_readtext ,       /* ignored but needed */
                          0 ,                     /* decimal shift */
                          REND_color_opacity_CB , /* callback when changed */
                          NULL ,                  /* data for above */
                          MCW_av_substring_CB ,   /* text creation routine */
                          func_opacity_labels     /* data for above */
                        ) ;
   }

   /*--- toggle switches to control if we see function ---*/

   { char * see_overlay_label[1] = { "See Overlay" } ;
     char * cut_overlay_label[1] = { "Cutout Overlay" } ;
     char * kill_isolas_label[1] = { "Remove Isolas" } ;

     wfunc_see_overlay_bbox = new_MCW_bbox( wfunc_opacity_rowcol ,
                                            1 , see_overlay_label ,
                                            MCW_BB_check ,
                                            MCW_BB_noframe ,
                                            REND_see_overlay_CB , NULL ) ;

     wfunc_cut_overlay_bbox = new_MCW_bbox( wfunc_opacity_rowcol ,
                                            1 , cut_overlay_label ,
                                            MCW_BB_check ,
                                            MCW_BB_noframe ,
                                            REND_cut_overlay_CB , NULL ) ;

     wfunc_kill_isolas_bbox = new_MCW_bbox( wfunc_opacity_rowcol ,
                                            1 , kill_isolas_label ,
                                            MCW_BB_check ,
                                            MCW_BB_noframe ,
                                            REND_kill_isolas_CB , NULL ) ;
   }

   XtManageChild( wfunc_opacity_rowcol ) ;
   XtManageChild( wfunc_opacity_frame ) ;

   /*--- range controls ---*/

   wfunc_range_frame =
      XtVaCreateManagedWidget(
         "AFNI" , xmFrameWidgetClass , wfunc_choices_rowcol ,
            XmNshadowType , XmSHADOW_ETCHED_IN ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;

   wfunc_range_rowcol =
      XtVaCreateWidget(
         "AFNI" , xmRowColumnWidgetClass , wfunc_range_frame ,
            XmNorientation , XmVERTICAL ,
            XmNpacking , XmPACK_TIGHT ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;

   /*--- label to show the data ranges from the overlay dataset ---*/

   xstr = REND_range_label() ;  /* make a dummy label */
   wfunc_range_label =
      XtVaCreateManagedWidget(
         "AFNI" , xmLabelWidgetClass , wfunc_range_rowcol ,
            XmNrecomputeSize , False ,
            XmNlabelString , xstr ,
            XmNtraversalOn , False ,
            XmNinitialResourcesPersistent , False ,
         NULL ) ;
   XmStringFree(xstr) ;

   /*--- toggle button to control automatic range scaling for pbar ---*/

 { char * range_bbox_label[1] = { "autoRange:xxxxxxxxx" } ;

   wfunc_range_bbox =
      new_MCW_bbox( wfunc_range_rowcol ,
                    1 , range_bbox_label ,
                    MCW_BB_check ,
                    MCW_BB_noframe ,
                    REND_range_bbox_CB , NULL ) ;

   MCW_set_bbox( wfunc_range_bbox , 1 ) ;

   xstr = REND_autorange_label() ;
   XtVaSetValues( wfunc_range_bbox->wbut[0], XmNlabelString,xstr , NULL ) ;
   XmStringFree(xstr) ;
 }

   /*--- arrowval to provide user control for pbar scaling ---*/

   wfunc_range_av =
      new_MCW_arrowval( wfunc_range_rowcol ,  /* parent */
                        NULL ,                /* label */
                        MCW_AV_downup ,       /* arrow directions */
                        0  ,                  /* min value */
                        9999999 ,             /* max value */
                        (int)(func_range) ,   /* init value */
                        MCW_AV_editext ,      /* input/output text display */
                        0 ,                   /* decimal shift */
                        REND_range_av_CB ,    /* routine to call when button */
                        NULL ,                /* is pressed, and its data */
                        NULL,NULL             /* no special display */
                     ) ;
   AV_SENSITIZE( wfunc_range_av , False ) ;

   XtManageChild( wfunc_range_rowcol ) ;
   XtManageChild( wfunc_range_frame ) ;

   XtManageChild( wfunc_choices_rowcol ) ;

   XtManageChild( wfunc_rowcol ) ;
   XtManageChild( wfunc_uber_rowcol ) ;

#if 0
   XtVaSetValues( wfunc_uber_rowcol , XmNresizeWidth , False , NULL ) ;
#endif

   return ;
}

/*---------------------------------------------------------------------------------
   Initialize the functional colormap
-----------------------------------------------------------------------------------*/

void REND_init_cmap(void)
{
   int ii , nc ;

   for( ii=0 ; ii < 127 ; ii++ )                             /* gray part */
      func_rmap[ii] = func_gmap[ii] = func_bmap[ii] = 2*ii ;

   func_rmap[127] = func_gmap[127] = func_bmap[127] = 255 ;  /* white pixel */

   nc = MIN( dc->ovc->ncol_ov , 129 ) ;   /* don't allow colormap overflow */

   for( ii=1 ; ii < nc ; ii++ ){                             /* color part */
      func_rmap[127+ii] = DCOV_REDBYTE(dc,ii) ;              /* [skips #0] */
      func_gmap[127+ii] = DCOV_GREENBYTE(dc,ii) ;
      func_bmap[127+ii] = DCOV_BLUEBYTE(dc,ii) ;
   }

   func_ncmap = 127 + nc ;                                   /* size of map */

   if( render_handle != NULL ){
       MREN_set_rgbmap( render_handle, func_ncmap, func_rmap,func_gmap,func_bmap ) ;
       func_cmap_set = 1 ;
   } else {
       func_cmap_set = 0 ;
   }

   return ;
}

/*---------------------------------------------------------------------------
   Open or close the overlay control panel
-----------------------------------------------------------------------------*/

void REND_open_func_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   if( wfunc_frame == NULL ) REND_func_widgets() ;  /* need to make them */

   if( XtIsManaged(wfunc_frame) ){          /* if open, close */
      XtUnmanageChild(wfunc_vsep ) ;
      XtUnmanageChild(wfunc_frame) ;
   } else {                                 /* if closed, open */
      HIDE_SCALE ;
      XtManageChild(wfunc_vsep ) ;
      XtManageChild(wfunc_frame) ;
      update_MCW_pbar( wfunc_color_pbar ) ; /* may need to be redrawn */
      FIX_SCALE_SIZE ;
      REND_init_cmap() ;                    /* setup the colormap */
   }

   MCW_invert_widget(wfunc_open_pb) ;       /* a flag */
   return ;
}

/*---------------------------------------------------------------------------
   Makes the labels for the thresh_top menu (narrower than the default)
-----------------------------------------------------------------------------*/

char * REND_thresh_tlabel_CB( MCW_arrowval * av , XtPointer junk )
{
   static char tlab[8] ;
   sprintf(tlab,"%d",av->ival) ;
   return tlab ;
}

/*---------------------------------------------------------------------------
   Initialize the memory of the color selector
-----------------------------------------------------------------------------*/

void REND_setup_color_pbar(void)
{
  MCW_pbar * pbar = wfunc_color_pbar ;
  int np , i , jm , lcol ;

  jm   = pbar->mode ;
  lcol = dc->ovc->ncol_ov - 1 ;

  /** load the 'save' values for all possible pane counts **/

  for( np=NPANE_MIN ; np <= NPANE_MAX ; np++ ){

      for( i=0 ; i <= np ; i++ ){
         pbar->pval_save[np][i][0] = INIT_pval_sgn[np][i] ;  /* from afni.h */
         pbar->pval_save[np][i][1] = INIT_pval_pos[np][i] ;
      }

      for( i=0 ; i <  np ; i++ ){
         pbar->ovin_save[np][i][0] = MIN( lcol , INIT_ovin_sgn[np][i] ) ;
         pbar->ovin_save[np][i][1] = MIN( lcol , INIT_ovin_pos[np][i] ) ;
      }
  }

  /** load the values for the current pane count **/

  np = pbar->num_panes ;
  jm = pbar->mode ;

  for( i=0 ; i <= np ; i++ ) pbar->pval[i]     = pbar->pval_save[np][i][jm] ;
  for( i=0 ; i <  np ; i++ ) pbar->ov_index[i] = pbar->ovin_save[np][i][jm] ;

  pbar->update_me = 1 ;
  return ;
}

/*-------------------------------------------------------------------------
  Create label for range display in function control panel
---------------------------------------------------------------------------*/

XmString REND_range_label(void)
{
   char fim_minch[10]  = " --------" , fim_maxch[10]  = " --------" ,
        thr_minch[10]  = " --------" , thr_maxch[10]  = " --------"   ;
   char buf[256] , qbuf[16] ;
   XmString xstr ;
   int iv ;

   if( ISVALID_DSET(func_dset) && ISVALID_STATISTIC(func_dset->stats) ){

      iv = func_color_ival ;

      if( DSET_VALID_BSTAT(func_dset,iv) ){
         AV_fval_to_char( func_dset->stats->bstat[iv].min , qbuf ) ;
         sprintf( fim_minch , "%9.9s" , qbuf ) ;
         AV_fval_to_char( func_dset->stats->bstat[iv].max , qbuf ) ;
         sprintf( fim_maxch , "%9.9s" , qbuf ) ;
      }

      iv = func_thresh_ival ;

      if( DSET_VALID_BSTAT(func_dset,iv) ){
         AV_fval_to_char( func_dset->stats->bstat[iv].min , qbuf ) ;
         sprintf( thr_minch , "%9.9s" , qbuf ) ;
         AV_fval_to_char( func_dset->stats->bstat[iv].max , qbuf ) ;
         sprintf( thr_maxch , "%9.9s" , qbuf ) ;
      }
   }

   sprintf( buf , "Color %s:%s\nThr   %s:%s" ,
            fim_minch,fim_maxch, thr_minch,thr_maxch ) ;

   xstr = XmStringCreateLtoR( buf , XmFONTLIST_DEFAULT_TAG ) ;

   return xstr ;
}

/*------------------------------------------------------------------------
   Find the autorange and make a label for the autorange control widget
--------------------------------------------------------------------------*/

XmString REND_autorange_label(void)
{
   XmString xstr ;
   float rrr = DEFAULT_FUNC_RANGE ;
   char buf[32] , qbuf[16] ;

   if( ISVALID_DSET(func_dset) ){

      RELOAD_STATS(func_dset) ;
      if( ISVALID_STATISTIC(func_dset->stats) ){
         float s1 , s2 ; int iv ;

         iv = func_color_ival ;

         if( DSET_VALID_BSTAT(func_dset,iv) ){
            s1  = fabs(func_dset->stats->bstat[iv].min) ,
            s2  = fabs(func_dset->stats->bstat[iv].max) ;
            rrr = (s1<s2) ? s2 : s1 ;
            if( rrr == 0.0 ) rrr = 1.0 ;
         }
      }
   }

   func_autorange = rrr ;
   AV_fval_to_char( rrr , qbuf ) ;
   sprintf( buf , "autoRange:%s" , qbuf ) ;
   xstr = XmStringCreateLtoR( buf , XmFONTLIST_DEFAULT_TAG ) ;

   return xstr ;
}

/*-----------------------------------------------------------------------------
   Set the p-value label at the bottom of the threshold slider
-------------------------------------------------------------------------------*/

void REND_set_thr_pval(void)
{
   float thresh , pval ;
   int   dec ;
   char  buf[16] ;

   if( !ISVALID_DSET(func_dset) ) return ;

   /* get the "true" threshold (scaled up from being in [0,1]) */

   thresh = func_threshold * func_thresh_top ;

   /* get the p-value that goes with this threshold, for this functional dataset */

#if 0
   if( ISFUNCBUCKET(func_dset) )
      pval = THD_stat_to_pval( thresh ,
                               DSET_BRICK_STATCODE(func_dset,func_thresh_ival) ,
                               DSET_BRICK_STATAUX (func_dset,func_thresh_ival)  ) ;

   else if( func_thresh_ival == DSET_THRESH_VALUE(func_dset) )
      pval = THD_stat_to_pval( thresh , func_dset->func_type ,
                                        func_dset->stat_aux   ) ;

   else
      pval = -1.0 ;
#else
   pval = THD_stat_to_pval( thresh ,
                            DSET_BRICK_STATCODE(func_dset,func_thresh_ival) ,
                            DSET_BRICK_STATAUX (func_dset,func_thresh_ival)  ) ;
#endif

   if( pval < 0.0 ){
      strcpy( buf , THR_PVAL_LABEL_NONE ) ;
   } else {
      if( pval == 0.0 ){
         strcpy( buf , "p = 0" ) ;
      } else if( pval >= 0.9999 ){
         strcpy( buf , "p = 1" ) ;
      } else if( pval >= 0.0010 ){
         char qbuf[16] ;
         sprintf( qbuf , "%5.4f" , pval ) ;
         strcpy( buf , qbuf+1 ) ;
      } else {
         int dec = (int)(0.999 - log10(pval)) ;
         pval = pval * pow( 10.0 , (double) dec ) ;  /* between 1 and 10 */
         if( dec < 10 ) sprintf( buf , "%3.1f-%1d" ,      pval, dec ) ;
         else           sprintf( buf , "%1d.-%2d"  , (int)pval, dec ) ;
      }
   }
   MCW_set_widget_label( wfunc_thr_pval_label , buf ) ;
   return ;
}

/*------------------------------------------------------------------------------
   When the user alters the threshold [at the end]
--------------------------------------------------------------------------------*/

void REND_thr_scale_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   XmScaleCallbackStruct * cbs = (XmScaleCallbackStruct *) call_data ;
   float fff ;

   fff = THR_FACTOR * cbs->value ;  /* between 0 and 1 now */
   if( fff >= 0.0 && fff <= 1.0 ) func_threshold = fff ; else return ;
   REND_set_thr_pval() ;

   INVALIDATE_OVERLAY ;
   return ;
}

/*-------------------------------------------------------------------------------
  When the user drags the slider (just update the p-value)
---------------------------------------------------------------------------------*/

void REND_thr_scale_drag_CB( Widget w, XtPointer client_data, XtPointer call_data )
{
   XmScaleCallbackStruct * cbs = (XmScaleCallbackStruct *) call_data ;
   float fff ;

   fff = THR_FACTOR * cbs->value ;  /* between 0 and 1 now */
   if( fff >= 0.0 && fff <= 1.0 ) func_threshold = fff ; else return ;
   REND_set_thr_pval() ;

   return ;
}


/*-------------------------------------------------------------------------------
  Called when the user toggles the autorange button
---------------------------------------------------------------------------------*/

void REND_range_bbox_CB( Widget w, XtPointer cd, XtPointer cb)
{
   int newauto = MCW_val_bbox(wfunc_range_bbox) ;

   if( newauto == func_use_autorange ) return ;  /* no change? */

   func_use_autorange = newauto ;

   func_range = (newauto) ? (func_autorange)
                          : (wfunc_range_av->fval) ;

   AV_SENSITIZE( wfunc_range_av , ! newauto ) ;

   INVALIDATE_OVERLAY ;
   return ;
}


/*------------------------------------------------------------------------------
  Called when the user changes the function range
--------------------------------------------------------------------------------*/

void REND_range_av_CB( MCW_arrowval * av , XtPointer cd )
{
   func_range = av->fval ;

   INVALIDATE_OVERLAY ;
   return ;
}

/*------------------------------------------------------------------------------
   Called to change the scaling on the threshold slider
--------------------------------------------------------------------------------*/

void REND_thresh_top_CB( MCW_arrowval * av , XtPointer cd )
{
   static float dval[9] = { 1.0 , 10.0 , 100.0 , 1000.0 , 10000.0 ,
                            100000.0 , 1000000.0 , 10000000.0 , 100000000.0 } ;
   int decim ;
   float tval ;

   tval = dval[av->ival] ; if( tval <= 0.0 ) tval = 1.0 ;

   decim = (2*THR_TOP_EXPON) - (int)(THR_TOP_EXPON + 0.01 + log10(tval)) ;
   if( decim < 0 ) decim = 0 ;

   XtVaSetValues( wfunc_thr_scale, XmNdecimalPoints, decim, NULL ) ;

   func_thresh_top = tval ;
   REND_set_thr_pval() ;

   INVALIDATE_OVERLAY ;
   return ;
}

/*-------------------------------------------------------------------------------
  Called by the pbar routines when the pbar is altered.
---------------------------------------------------------------------------------*/

void REND_color_pbar_CB( MCW_pbar * pbar , XtPointer cd , int reason )
{
   FIX_SCALE_SIZE ;
   INVALIDATE_OVERLAY ;
   return ;
}

/*------------------------------------------------------------------------------
  Called to change the number of panels in the pbar
--------------------------------------------------------------------------------*/

void REND_colornum_av_CB( MCW_arrowval * av , XtPointer cd )
{
   HIDE_SCALE ;
   alter_MCW_pbar( wfunc_color_pbar , av->ival , NULL ) ;
   FIX_SCALE_SIZE ;
   INVALIDATE_OVERLAY ;
   return ;
}

/*------------------------------------------------------------------------------
   Called when the user toggles the posfunc button
--------------------------------------------------------------------------------*/

void REND_color_bbox_CB( Widget w, XtPointer cd, XtPointer cb)
{
   int jm , newpos=MCW_val_bbox(wfunc_color_bbox) ;

   if( newpos == func_posfunc ) return ;  /* no change? */

   func_posfunc = newpos ;
   jm = wfunc_color_pbar->mode = (newpos) ? 1 : 0 ;  /* pbar mode */

   HIDE_SCALE ;
   alter_MCW_pbar( wfunc_color_pbar , wfunc_color_pbar->npan_save[jm] , NULL ) ;
   FIX_SCALE_SIZE ;

   /* set the count on the pbar control arrowval to match */

   AV_assign_ival( wfunc_colornum_av , wfunc_color_pbar->npan_save[jm] ) ;

   INVALIDATE_OVERLAY ;
   return ;
}

/*------------------------------------------------------------------------------
   Called to set the overlay opacity factor
--------------------------------------------------------------------------------*/

void REND_color_opacity_CB( MCW_arrowval * av , XtPointer cd )
{
   func_color_opacity = 0.1 * av->ival ;

   INVALIDATE_OVERLAY ;
   return ;
}

/*------------------------------------------------------------------------------
   Called to toggle visibility of the overlay
--------------------------------------------------------------------------------*/

void REND_see_overlay_CB( Widget w, XtPointer cd, XtPointer cb)
{
   int newsee = MCW_val_bbox(wfunc_see_overlay_bbox) ;

   if( newsee == func_see_overlay ) return ;

   func_see_overlay = newsee ;
   INVALIDATE_OVERLAY ; FREE_VOLUMES ;
   return ;
}

/*------------------------------------------------------------------------------
   Called to toggle whether or not cutouts affect the overlay
--------------------------------------------------------------------------------*/

void REND_cut_overlay_CB( Widget w, XtPointer cd, XtPointer cb)
{
   int newcut = MCW_val_bbox(wfunc_cut_overlay_bbox) ;

   if( newcut == func_cut_overlay ) return ;

   func_cut_overlay = newcut ;
   if( num_cutouts > 0 ){ INVALIDATE_OVERLAY ; }
   return ;
}

/*------------------------------------------------------------------------------
   Called to toggle whether or not to kill isolated voxels (isolas)
--------------------------------------------------------------------------------*/

void REND_kill_isolas_CB( Widget w, XtPointer cd, XtPointer cb)
{
   int newkill = MCW_val_bbox(wfunc_kill_isolas_bbox) ;

   if( newkill == func_kill_isolas ) return ;

   func_kill_isolas = newkill ;
   INVALIDATE_OVERLAY ;
   return ;
}

/*-------------------------------------------------------------------------------
  Event handler to find #3 button press for pbar popup, and popup the menu
---------------------------------------------------------------------------------*/

void REND_pbarmenu_EV( Widget w , XtPointer cd ,
                       XEvent * ev , Boolean * continue_to_dispatch )
{
   static int old_paltab_num = 0 ;

   switch( ev->type ){
      case ButtonPress:{
         XButtonEvent * event = (XButtonEvent *) ev ;
         if( event->button == Button3 ){

            /* in case the user read in any new palette, add them to menu */

            if( GPT != NULL && PALTAB_NUM(GPT) > old_paltab_num ){
               refit_MCW_optmenu( wfunc_pbar_palette_av ,
                                    0 ,                     /* new minval */
                                    PALTAB_NUM(GPT)-1 ,     /* new maxval */
                                    0 ,                     /* new inival */
                                    0 ,                     /* new decim? */
                                    AFNI_palette_label_CB , /* text routine */
                                    NULL                    /* text data */
                                 ) ;
               XtManageChild( wfunc_pbar_palette_av->wrowcol ) ;
               old_paltab_num = PALTAB_NUM(GPT) ;
            }

            XmMenuPosition( wfunc_pbar_menu , event ) ; /* where */
            XtManageChild ( wfunc_pbar_menu ) ;         /* popup */
         }
      }
      break ;
   }
   return ;
}

/*--------------------------------------------------------------------------------
  Callbacks for all actions in the pbar popup
----------------------------------------------------------------------------------*/

void REND_pbarmenu_CB( Widget w , XtPointer cd , XtPointer cbs )
{
   MCW_pbar * pbar ;
   int npane , jm , ii ;
   double pmax , pmin ;
   float pval[NPANE_MAX+1] ;

   pbar  = wfunc_color_pbar ;
   npane = pbar->num_panes ;
   jm    = pbar->mode ;
   pmax  = pbar->pval_save[npane][0][jm] ;
   pmin  = pbar->pval_save[npane][npane][jm] ;

   /*--- Equalize spacings ---*/

   if( w == wfunc_pbar_equalize_pb ){
      for( ii=0 ; ii <= npane ; ii++ )
         pval[ii] = pmax - ii * (pmax-pmin)/npane ;

      HIDE_SCALE ;
      alter_MCW_pbar( pbar , 0 , pval ) ;
      FIX_SCALE_SIZE ;
      INVALIDATE_OVERLAY ;
   }

   /*--- Set top value ---*/

   else if( w == wfunc_pbar_settop_pb ){
      MCW_choose_integer( wfunc_choices_label ,
                          "Pbar Top" , 0 , 99999 , 1 ,
                          REND_set_pbar_top_CB , NULL  ) ;
   }

   return ;
}

void REND_palette_av_CB( MCW_arrowval * av , XtPointer cd )
{
   if( GPT == NULL || av->ival < 0 || av->ival >= PALTAB_NUM(GPT) ) return ;

   HIDE_SCALE ;
   load_PBAR_palette_array( wfunc_color_pbar ,             /* cf. afni_setup.c */
                            PALTAB_ARR(GPT,av->ival) , 0 ) ;
   FIX_SCALE_SIZE ;

   INVALIDATE_OVERLAY ;
   return ;
}

void REND_set_pbar_top_CB( Widget w , XtPointer cd , MCW_choose_cbs * cbs )
{
   MCW_pbar * pbar ;
   float pval[NPANE_MAX+1] ;
   double pmax , fac ;
   int ii ;

   if( ! renderer_open ){ POPDOWN_integer_chooser; XBell(dc->display,100); return; }

   pmax = cbs->fval ; if( pmax <= 0.0 ) return ;           /* illegal */
   pbar = wfunc_color_pbar ;
   fac  = pmax / pbar->pval[0] ; if( fac == 1.0 ) return ; /* no change */

   for( ii=0 ; ii <= pbar->num_panes ; ii++ )
      pval[ii] = fac * pbar->pval[ii] ;

   HIDE_SCALE ;
   alter_MCW_pbar( pbar , 0 , pval ) ;
   FIX_SCALE_SIZE ;

   INVALIDATE_OVERLAY ;
   return ;
}

/*-----------------------------------------------------------------------------------
   Load color index data from the functional dataset into the local overlay array
   (heavily adapted from AFNI_func_overlay in afni_func.c).
-------------------------------------------------------------------------------------*/

void REND_reload_func_dset(void)
{
   MRI_IMAGE * cim , * tim ;
   void      * car , * tar ;
   float      cfac ,  tfac ;
   int ii , nvox , num_lp , lp ;
   byte * ovar ;
   MCW_pbar * pbar = wfunc_color_pbar ;
   byte fim_ovc[NPANE_MAX+1] ;
   float fim_thr[NPANE_MAX] , scale_factor , thresh ;

   if( func_dset == NULL ) return ;  /* error */

   INVALIDATE_OVERLAY ;              /* toss old overlay, if any */

   DSET_load(func_dset) ;            /* make sure is in memory */

   cim  = DSET_BRICK(func_dset,func_color_ival) ; nvox = cim->nvox ; /* color brick */
   car  = DSET_ARRAY(func_dset,func_color_ival) ;
   cfac = DSET_BRICK_FACTOR(func_dset,func_color_ival) ;
   if( cfac == 0.0 ) cfac = 1.0 ;

   tim  = DSET_BRICK(func_dset,func_thresh_ival) ;                   /* thresh brick */
   tar  = DSET_ARRAY(func_dset,func_thresh_ival) ;
   tfac = DSET_BRICK_FACTOR(func_dset,func_thresh_ival) ;
   if( tfac == 0.0 ) tfac = 1.0 ;

   ovim = mri_new_conforming( cim , MRI_byte ) ;                     /* new overlay */
   ovar = MRI_BYTE_PTR(ovim) ;

   scale_factor = func_range ;                                       /* for map from */
   if( scale_factor == 0.0 || func_use_autorange )                   /* pbar to data */
      scale_factor = func_autorange ;                                /* value range  */

   num_lp = pbar->num_panes ;
   for( lp=0 ; lp < num_lp ; lp++ ) fim_ovc[lp] = pbar->ov_index[lp] ; /* top to bottom */
   fim_ovc[num_lp] = (func_posfunc) ? (0) : (fim_ovc[num_lp-1]) ;      /* off the bottom */

   thresh = func_threshold * func_thresh_top / tfac ;  /* threshold in tar[] scale */

   /*--- Load the overlay image with the color overlay index ---*/

   if( thresh < 1.0 || !func_use_thresh ){  /*--- no thresholding needed ---*/

      switch( cim->kind ){

         case MRI_short:{
            short * sar = (short *) car ;

            for( lp=0 ; lp < num_lp ; lp++ )                          /* pbar in     */
               fim_thr[lp] = scale_factor * pbar->pval[lp+1] / cfac ; /* car[] scale */

            for( ii=0 ; ii < nvox ; ii++ ){
               if( sar[ii] == 0 ){
                  ovar[ii] = 0 ;
               } else {
                  for( lp=0 ; lp < num_lp && sar[ii] < fim_thr[lp] ; lp++ ) ; /*nada*/
                  ovar[ii] = fim_ovc[lp] ;
               }
            }
         }
         break ;

         case MRI_byte:{
            byte * sar = (byte *) car ;

            for( lp=0 ; lp < num_lp ; lp++ )
               if( pbar->pval[lp+1] <= 0.0 )
                  fim_thr[lp] = 0 ;
               else
                  fim_thr[lp] = scale_factor * pbar->pval[lp+1] / cfac ;

            for( ii=0 ; ii < nvox ; ii++ ){
               if( sar[ii] == 0 ){
                  ovar[ii] = 0 ;
               } else {
                  for( lp=0 ; lp < num_lp && sar[ii] < fim_thr[lp] ; lp++ ) ; /*nada*/
                  ovar[ii] = fim_ovc[lp] ;
               }
            }
         }
         break ;
      }             /*--- end of no thresholding case ---*/

   } else {         /*--- start of thresholding ---*/

      switch( cim->kind ){

         case MRI_short:{
            short * sar = (short *) car ;
            short * qar = (short *) tar ;
            short   thr = (short) thresh ;

            for( lp=0 ; lp < num_lp ; lp++ )
               fim_thr[lp] = scale_factor * pbar->pval[lp+1] / cfac ;

            for( ii=0 ; ii < nvox ; ii++ ){
               if( (qar[ii] > -thr && qar[ii] < thr) || sar[ii] == 0 ){
                  ovar[ii] = 0 ;
               } else {
                  for( lp=0 ; lp < num_lp && sar[ii] < fim_thr[lp] ; lp++ ) ; /*nada*/
                  ovar[ii] = fim_ovc[lp] ;
               }
            }
         }
         break ;

         case MRI_byte:{
            byte * sar = (byte *) car ;
            byte * qar = (byte *) tar ;
            byte   thr = (byte) thresh ;

            for( lp=0 ; lp < num_lp ; lp++ )
               if( pbar->pval[lp+1] <= 0.0 )
                  fim_thr[lp] = 0 ;
               else
                  fim_thr[lp] = scale_factor * pbar->pval[lp+1] / cfac ;

            for( ii=0 ; ii < nvox ; ii++ ){
               if( qar[ii] < thr || sar[ii] == 0 ){
                  ovar[ii] = 0 ;
               } else {
                  for( lp=0 ; lp < num_lp && sar[ii] < fim_thr[lp] ; lp++ ) ; /*nada*/
                  ovar[ii] = fim_ovc[lp] ;
               }
            }
         }
         break ;
      }
   }

   /*----- if ordered, remove isolas -----*/

   if( func_kill_isolas ){
      int nx=ovim->nx , ny=ovim->ny , nz=ovim->nz , nxy=nx*ny , ntop=nvox-nxy ;

      for( ii=nxy ; ii < ntop ; ii++ ){
         if( ovar[ii    ] != 0 &&
             ovar[ii-1  ] == 0 && ovar[ii+1  ] == 0 &&
             ovar[ii-nx ] == 0 && ovar[ii+nx ] == 0 &&
             ovar[ii-nxy] == 0 && ovar[ii+nxy] == 0    ) ovar[ii] = 0 ;
      }
   }

   return ;
}

/*---------------------------------------------------------------------------
  Callback for opacity scale factor selection
-----------------------------------------------------------------------------*/

void REND_opacity_scale_CB( MCW_arrowval * av , XtPointer cd )
{
   if( av->fval < MIN_OPACITY_SCALE ) AV_assign_fval(av,MIN_OPACITY_SCALE) ;
   if( dynamic_flag && render_handle != NULL ) REND_draw_CB(NULL,NULL,NULL) ;
   return ;
}
