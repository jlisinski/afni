
/*****************************************************************************
  This software is copyrighted and owned by the Medical College of Wisconsin.
  See the file README.Copyright for details.
******************************************************************************/

#include "mrilib.h"
#include "coxplot.h"
#include "display.h"

/*---- quickie program to look at some graphs - RWCox - Feb 1999 ----*/

#define DEFAULT_NCOLOVR 20

static char * INIT_colovr[DEFAULT_NCOLOVR] = {
   "#ffff00" , "#ffcc00"   , "#ff9900"  , "#ff6900" , "#ff4400" , "#ff0000" ,
   "#0000ff" , "#0044ff"   , "#0069ff"  , "#0099ff" , "#00ccff" , "#00ffff" ,
   "green"   , "limegreen" , "violet"   , "hotpink" ,
   "white"   , "#dddddd"   , "#bbbbbb"  , "black"
} ;

static char * INIT_labovr[DEFAULT_NCOLOVR] = {
   "yellow" , "yell-oran" , "oran-yell" , "orange"   , "oran-red" , "red"   ,
   "dk-blue", "blue"      , "lt-blue1"  , "lt-blue2" , "blue-cyan", "cyan"  ,
   "green"  , "limegreen" , "violet"    , "hotpink"  ,
   "white"  , "gry-dd"    , "gry-bb"    , "black"
} ;

static int nx,nts , sep=1 ;
static float ** yar , * xar ;
static MCW_DC * dc ;
static char * title = NULL , * xlabel = NULL , * ylabel = NULL ;

static char * dfile_nar[6] = {
         "Roll [\\degree]" , "Pitch [\\degree]" , "Yaw [\\degree]"    ,
         "\\Delta I-S [mm]" , "\\Delta R-L [mm]" , "\\Delta A-P [mm]"  } ;

static int    nyar = 0 ;
static char * ynar[128] ;
static char ** yname = NULL ;

void startup_timeout_CB( XtPointer client_data , XtIntervalId * id ) ;

int main( int argc , char * argv[] )
{
   int iarg=1 , ii , ny , ignore=0 ;
   float dx=1.0 ;
   char * tsfile , * cpt ;
   char dname[THD_MAX_NAME] , subv[THD_MAX_NAME] ;
   MRI_IMAGE * inim , * flim ;
   float * far ;
   XtAppContext app ;
   Widget shell ;

   /*-- help? --*/

   if( argc < 2 || strcmp(argv[1],"-help") == 0 ){
     printf("Usage: 1dplot [options] tsfile\n"
            "Graphs the columns of a *.1D type time series file to the screen.\n"
            "\n"
            "Options:\n"
            " -sep       = Plot each column in a separate sub-graph.\n"
            " -one       = Plot all columns together in one big graph.\n"
            "                [default = -sep]\n"
            " -dx xx     = Spacing between points on the x-axis is 'xx'\n"
            "                [default = 1]\n"
            " -ignore nn = Skip first 'nn' rows in the input file\n"
            "                [default = 0]\n"
            " -xlabel aa = Put string 'aa' below the x-axis\n"
            "                [default = no axis label]\n"
            " -ylabel aa = Put string 'aa' to the left of the y-axis\n"
            "                [default = no axis label]\n"
            "\n"
            " -ynames aa bb ... = Use the strings 'aa', 'bb', etc., as\n"
            "                     labels to the right of the graphs,\n"
            "                     corresponding to each input column.\n"
            "                     These strings CANNOT start with the\n"
            "                     '-' character.\n"
            "\n"
            " -volreg           = Makes the 'ynames' be the same as the\n"
            "                     6 labels used in plug_volreg for\n"
            "                     Roll, Pitch, Yaw, I-S, R-L, and A-P\n"
            "                     movements, in that order.\n"
            "\n"
            "You may also select a subset of columns to display using\n"
            "a tsfile specification like 'fred.1D[0,3,5]', indicating\n"
            "that columns #0, #3, and #5 will be the only ones plotted.\n"
            "For more details on this selection scheme, see the output\n"
            "of '3dcalc -help'.\n"
            "\n"
            "Example: graphing a 'dfile' output by 3dvolreg, when TR=5:\n"
            "   1dplot -volreg -dx 5 -xlabel Time 'dfile[1..6]'\n"
            "\n"
           ) ;
      exit(0) ;
   }

   /* open X11 */

   shell = XtVaAppInitialize(
              &app , "AFNI" , NULL , 0 , &argc , argv , NULL , NULL ) ;
   if( shell == NULL ){
      fprintf(stderr,"** Cannot initialize X11!\n") ; exit(1) ;
   }

   dc = MCW_new_DC( shell , 16 ,
                    DEFAULT_NCOLOVR , INIT_colovr , INIT_labovr ,
                    1.0 , 0 ) ;

   /*-- scan arguments that X11 didn't eat --*/

   while( iarg < argc && argv[iarg][0] == '-' ){

     if( strcmp(argv[iarg],"-") == 0 ){  /* skip */
        iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-ynames") == 0 ){
        iarg++ ;
        while( iarg < argc && argv[iarg][0] != '-' ){
           ynar[nyar++] = argv[iarg++] ;
        }
        continue ;
     }

     if( strcmp(argv[iarg],"-volreg") == 0 ){
        int ii ;
        for( ii=0 ; ii < 6 ; ii++ ) ynar[nyar++] = dfile_nar[ii] ;
        iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-title") == 0 ){
        title = argv[++iarg] ;
        iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-xlabel") == 0 ){
        xlabel = argv[++iarg] ;
        iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-ylabel") == 0 ){
        ylabel = argv[++iarg] ;
        iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-ignore") == 0 ){
        ignore = strtod( argv[++iarg] , NULL ) ;
        if( ignore < 0 ){fprintf(stderr,"** Illegal -ignore value!\n");exit(1);}
        iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-dx") == 0 ){
        dx = strtod( argv[++iarg] , NULL ) ;
        if( dx <= 0.0 ){fprintf(stderr,"** Illegal -dx value!\n");exit(1);}
        iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-sep") == 0 ){
        sep = 1 ; iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-one") == 0 ){
        sep = 0 ; iarg++ ; continue ;
     }

     fprintf(stderr,"** Unknown option: %s\n",argv[iarg]) ; exit(1) ;
   }

   if( iarg >= argc ){
      fprintf(stderr,"** No tsfile on command line!\n") ; exit(1) ;
   }

   if( nyar > 0 ) yname = ynar ;

   /* check input filename for index strings */

   tsfile = argv[iarg] ;
   cpt    = strstr(tsfile,"[") ;

   if( cpt == NULL ){
      strcpy( dname , tsfile ) ;
      subv[0] = '\0' ;
   } else if( cpt == tsfile ){
      fprintf(stderr,"** Illegal filename on command line!\n");exit(1);
   } else {
      ii = cpt - tsfile ;
      memcpy(dname,tsfile,ii) ; dname[ii] = '\0' ;
      strcpy(subv,cpt) ;
   }

   /* read input file */

   inim = mri_read_ascii( dname ) ;
   if( inim == NULL ){
      fprintf(stderr,"** Can't read input file %s\n",dname) ;
      exit(1);
   }

   if( inim->kind != MRI_float ){  /* should not happen */
      flim = mri_to_float(inim) ; mri_free(inim) ; inim = flim ;
   }
   flim = mri_transpose(inim) ; mri_free(inim) ;
   far  = MRI_FLOAT_PTR(flim) ;
   nx   = flim->nx ;
   ny   = flim->ny ;

   /* make x axis */

   xar = (float *) malloc( sizeof(float) * nx ) ;
   for( ii=0 ; ii < nx ; ii++ ) xar[ii] = dx * ii ;

   /* select data to plot */

   if( subv[0] == '\0' ){  /* no sub-list */

      nts = ny ;
      yar = (float **) malloc(sizeof(float *)*nts) ;
      for( ii=0 ; ii < ny ; ii++ ) yar[ii] = far + (ii*nx+ignore) ;

   } else {                /* process sub-list */
      int  * ivlist , * ivl ;

      ivlist = MCW_get_intlist( ny , subv ) ;
      if( ivlist == NULL || ivlist[0] < 1 ){
         fprintf(stderr,"** Illegal column selectors on command line!\n");
         exit(1);
      }
      nts = ivlist[0] ;
      ivl = ivlist + 1 ;
      for( ii=0 ; ii < nts ; ii++ ){
         if( ivl[ii] < 0 || ivl[ii] >= ny ){
            fprintf(stderr,"** Illegal selector on command line!\n");
            exit(1) ;
         }
      }
      yar = (float **) malloc(sizeof(float *)*nts) ;
      for( ii=0 ; ii < nts ; ii++ ) yar[ii] = far + (ivl[ii]*nx+ignore) ;
      free(ivlist) ;
   }

   nx = nx - ignore ;  /* cut off the ignored points */

   /* start X11 */

   (void) XtAppAddTimeOut( app , 123 , startup_timeout_CB , NULL ) ;

   XtAppMainLoop(app) ;
   exit(0) ;
}

/*-----------------------------------------------------------------*/
void killfunc(void * fred){ exit(0) ; }
/*-----------------------------------------------------------------*/

void startup_timeout_CB( XtPointer client_data , XtIntervalId * id )
{
   int ng ;

   /* make graph */

   ng = (sep) ? (-nts) : (nts) ;
   
   plot_ts_lab( dc->display , nx , xar , ng , yar ,
                xlabel , ylabel , title , yname , killfunc ) ;

   return ;
}
