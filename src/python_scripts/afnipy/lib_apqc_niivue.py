#!/usr/bin/env python

# This library contains functions for creating part of the
# afni_proc.py QC HTML.  Specifically, these functions build the
# NiiVue-specific parts
#
# =======================================================================

import os, sys
import copy, json

# Dictionary for converting an AFNI cbar name (keys) to NiiVue cmap
# name(s). Note that NiiVue will split bi-directional pbars into 2
# cmaps (pos first, then neg), so we always make a list of any mapping.
# !!! TODO:  UPDATE WITH AFNI CBARS, WHEN AVAILABLE
afni2nv_cmaps = {
    'Plasma' : ['plasma'],
    #'Reds_and_Blues_Inv' : ['afni_reds_inv', 'afni_blues_inv'],
    'Reds_and_Blues_Inv' : ['redyell', 'bluegrn'],
}



def translate_pbar_dict_afni2nv(pbar_dict):
    """Take a pbar_dict from @chauffeur_afni and translate pieces for NiiVue

Parameters
----------
pbar_dict : dict
    pbar dictionary output by @chauffeur_afni

Returns
-------
nv_dict : dict
    dictionary of ulay, olay, thr and colorizing info for niivue to use

"""

    # start the niivue dict
    nv_dict = {}

    # subbricks of ulay, olay and thr, respectively
    subbb   = copy.deepcopy(pbar_dict['subbb'])

    # cmap or cmaps in niivue
    cmap = copy.deepcopy(afni2nv_cmaps[pbar_dict['cbar']])

    # is there an overlay to display?
    nv_dict['see_overlay'] = pbar_dict['see_olay']

    if pbar_dict['see_olay'] == '+' :
        nv_dict['cmap']     = cmap[0]                 # cbar name(s)
        if len(cmap) > 1 :
            nv_dict['cmap_neg'] = cmap[1]             # cbar name(s)
        nv_dict['idx_olay'] = subbb[1]                # idx of olay subbrick
        nv_dict['idx_thr']  = subbb[2]                # idx of thr subbrick
        nv_dict['cal_min']  = pbar_dict['vthr']       # thr
        nv_dict['cal_max']  = pbar_dict['pbar_top']   # cbar max

        # if pbar range is [-X, X] and not [0, X]
        if pbar_dict['pbar_bot'] != 0.0 :
            nv_dict['cal_minNeg'] = - nv_dict['cal_min']
            nv_dict['cal_maxNeg'] = pbar_dict['pbar_bot']

        if pbar_dict['olay_alpha'] != 'No' :          # use alpha fade?
            nv_dict['do_alpha'] = 'true'
        else:
            nv_dict['do_alpha'] = 'false'

        if pbar_dict['olay_boxed'] != 'No' :          # use box lines
            nv_dict['do_boxed'] = 1
        else:
            nv_dict['do_boxed'] = 0

    return nv_dict

def set_niivue_ulay(nv_dict):
    """Use the NiiVue dict to build the NVOBJ.loadVolumes([..]) info for
the ulay dset.

Parameters
----------
nv_dict : dict
    dictionary of ulay, olay, thr and colorizing info for niivue to use

Returns
-------
nv_ulay_txt : str
    text for the underlay info in loading a NiiVue volume

    """

    nv_ulay_txt = '''      {{ // ulay
        url:"{ulay}",
      }}'''.format( **nv_dict )

    return nv_ulay_txt

def set_niivue_olay(nv_dict):
    """Use the NiiVue dict to build the NVOBJ.loadVolumes([..]) info for
the olay dset.

Parameters
----------
nv_dict : dict
    dictionary of ulay, olay, thr and colorizing info for niivue to use

Returns
-------
nv_olay_txt : str
    text for the overlay (and thr) info in loading a NiiVue volume

    """

    nv_olay_txt = ''', {{ // olay
        url:"{olay}",
        colorMap: "{cmap}",'''.format(**nv_dict)

    if 'cmap_neg' in nv_dict :
        nv_olay_txt+= '''
        colorMapNegative: "{cmap_neg}",'''.format(**nv_dict)

    nv_olay_txt+= '''
        cal_min: {cal_min},
        cal_max: {cal_max},
      }}'''.format(**nv_dict)

    return nv_olay_txt

def set_niivue_thr(nv_dict):
    """Use the NiiVue dict to build the NVOBJ.loadVolumes([..]) info for
the thr dset.

Parameters
----------
nv_dict : dict
    dictionary of ulay, olay, thr and colorizing info for niivue to use

Returns
-------
nv_thr_txt : str
    text for the thr info in loading a NiiVue volume

    """

    # thr+olay dsets are same, but might be diff subbricks
    nv_thr_txt = ''', {{ // thr
        url:"{olay}",
        colorMap: "{cmap}",'''.format(**nv_dict)

    if 'cmap_neg' in nv_dict :
        nv_thr_txt+= '''
        colorMapNegative: "{cmap_neg}",'''.format(**nv_dict)

    nv_thr_txt+= '''
        cal_min: {cal_min},
        cal_max: {cal_max},
      }}'''.format(**nv_dict)

    return nv_thr_txt


def set_niivue_then(nv_dict):
    """Use the NiiVue dict to build the 'then' text after
NVOBJ.loadVolumes([..]).

Parameters
----------
nv_dict : dict
    dictionary of ulay, olay, thr and colorizing info for niivue to use

Returns
-------
nv_then_txt : str
    text for the 'then' features in loading a NiiVue volume

    """


    # !!!! TODO:  add in negative colorbar stuff, using if conditions
    nv_then_txt = '''
      {nobj}.setFrame4D({nobj}.volumes[1].id, {idx_olay}); // olay
      {nobj}.setFrame4D({nobj}.volumes[2].id, {idx_thr});  // thr
      {nobj}.volumes[0].colorbarVisible = false; 
      {nobj}.volumes[1].alphaThreshold = {do_alpha};
      {nobj}.overlayOutlineWidth = {do_boxed};
      {nobj}.opts.multiplanarForceRender = true;
      {nobj}.backgroundMasksOverlays = true;
      {nobj}.updateGLVolume();'''.format(**nv_dict)


    return nv_then_txt


# =======================================================================

def make_niivue_2dset(ulay_name, pbar_dict, olay_name=None, itemid='',
                      path='..', verb=0):
    """

Parameters
----------
ulay_name : str
    name of ulay dataset
pbar_dict : dict
    pbar (=cmap = cbar) dictionary of items; made by converting
    @chauffeur_afni's '-pbar_saveim ..' txt file -> JSON and reading
    it in as a dict. Contains both ulay and olay (if present)
    information
olay_name : str
    name of olay dataset
itemid : str
    ID of the div that will be activated using this NiiVue instance
path : str
    path to the data, which will typically be the default
verb : str
    verbosity level whilst processing

Returns
-------
otxt : str
    string of the NiiVue canvas to stick into the HTML

    """

    # Make the dsets with paths
    pdata = path.rstrip('/')
    if ulay_name[0] == '/' : 
        # the template, which can only be a ulay, could be abs path
        ulay = ulay_name
    else:
        ulay  = pdata + '/' + ulay_name
    if olay_name :  olay = pdata + '/' + olay_name

    # NiiVue canvas ID
    nid = 'nvcan_' + itemid
    nobj = 'nvobj_' + itemid

    # Setup a NiiVue dict for loading and displaying volumes, based on
    # the AFNI pbar.  Translate names of things, and load in various
    # other useful pieces of information for string generation
    nv_dict = translate_pbar_dict_afni2nv(pbar_dict)
    nv_dict['ulay'] = ulay
    if olay :  nv_dict['olay'] = olay
    nv_dict['nobj'] = nobj

    # create pieces of text within NiiVue canvas
    nv_ulay_txt = set_niivue_ulay(nv_dict)
    nv_olay_txt = set_niivue_olay(nv_dict)
    nv_thr_txt  = set_niivue_thr(nv_dict)
    nv_then_txt = set_niivue_then(nv_dict)


    # !!! TODO:  *** ADD NEAR TOP of HTML, and change path to abin***
    '''
    <script src="./niivue.umd.js"> </script>
    '''

    otxt = '''
<!-- start NiiVue canvas for: {nid} -->
<div class="class_niivue">
  <canvas id="{nid}" height=480 width=640>
  </canvas>
  <script>
'''.format( nid=nid )

    otxt+= '''
    const {nobj} = new niivue.Niivue(
      {{ logging:true, 
        show3Dcrosshair: true }}
    )
    {nobj}.opts.isColorbar = true
    {nobj}.attachTo('{nid}')
    {nobj}.loadVolumes([
{nv_ulay_txt}{nv_olay_txt}{nv_thr_txt}
    ]).then(()=>{{ // more actions
{nv_then_txt}
    }})'''.format( nobj=nobj, nid=nid, 
                   nv_ulay_txt=nv_ulay_txt, nv_olay_txt=nv_olay_txt,
                   nv_thr_txt=nv_thr_txt,
                   nv_then_txt=nv_then_txt )

    otxt+= '''
  </script>
</div>
'''

    if verb :
        tmp = "="*20 + ' NiiVue canvas ' + "="*20
        print("-"*len(tmp))
        print(otxt)
        print("-"*len(tmp))

    return otxt


'''
<!-- niivue canvas -->
<div style="width: 90%; aspect-ratio: 5 / 1; margin-left: auto; margin-right: auto;">
  <canvas id="nv_vis_0_Coef" height=480 width=640>
  </canvas>
  <script>
    const nv_vis_0_Coef = new niivue.Niivue({logging:true, show3Dcrosshair: true})
    nv_vis_0_Coef.opts.isColorbar = true
    nv_vis_0_Coef.attachTo('nv_vis_0_Coef')
    nv_vis_0_Coef.loadVolumes([
      {url:"../anat_final.sub-001+tlrc.HEAD"},
      {
        url:"../stats.sub-001+tlrc.HEAD",
        colorMap: "redyell",
        colorMapNegative: "bluegrn",
        cal_min: 3.37,
        cal_max: 5.64
      },
      {
        url:"../stats.sub-001+tlrc.HEAD",
        colorMap: "redyell",
        colorMapNegative: "bluegrn",
        cal_min: 3.37,
        cal_max: 5.64
      }
    ]).then(()=>{
      nv_vis_0_Coef.setFrame4D(nv_vis_0_Coef.volumes[1].id, 1)
      nv_vis_0_Coef.setFrame4D(nv_vis_0_Coef.volumes[2].id, 2)
      nv_vis_0_Coef.volumes[1].alphaThreshold = true
      nv_vis_0_Coef.volumes[2].alphaThreshold = true
      nv_vis_0_Coef.overlayOutlineWidth = 1
      nv_vis_0_Coef.volumes[0].colorbarVisible = false; //hide colorbar for anatomical scan
      nv_vis_0_Coef.volumes[2].colorbarVisible = false; //hide colorbar for tstat scan
      nv_vis_0_Coef.opts.multiplanarForceRender = true;
      nv_vis_0_Coef.backgroundMasksOverlays = true
      nv_vis_0_Coef.updateGLVolume()
    })
'''





