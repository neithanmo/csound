#include <OpcodeBase.hpp>
#include <map>
#include <vector>

/**
 * The busses are laid out: 
 * busses[bus][channel][frame].
 */
static std::map<size_t, std::vector< std::vector<MYFLT> > > busses;

/**
 * The mixer matrix is laid out: 
 * matrix[send][bus].
 */
static std::map<size_t, std::map<size_t, MYFLT> > matrix;

/**
 * MixerSetLevel isend, ibuss, kgain
 *
 * Controls the gain of any signal route from a send to a bus.
 * Also creates the bus if it does not exist.
 */
struct MixerSetLevel : public OpcodeBase<MixerSetLevel>
{
  // No outputs.
  // Inputs.
  MYFLT *isend;
  MYFLT *ibuss;
  MYFLT *kgain;
  // State.
  size_t send;
  size_t buss;
  int init(ENVIRON *csound)
  {
    //warn(csound, "MixerSetLevel::init...\n");
    send = static_cast<size_t>(*isend);
    buss = static_cast<size_t>(*ibuss);
    if(busses.find(buss) == busses.end())
      {
	size_t channels = csound->GetNchnls(csound);
	size_t frames = csound->GetKsmps(csound);
	busses[buss].resize(channels);
	for(size_t channel = 0; channel < channels; channel++)
	  {
	    busses[buss][channel].resize(frames);
	  }
	//warn(csound, "MixerSetLevel::init: initialized buss %d channels %d frames %d\n", buss, channels, frames);
      }
    matrix[send][buss] = *kgain;
    //warn(csound, "MixerSetLevel::kontrol: send %d buss %d gain %f\n", send, buss, gain);
    return OK;
  }
  int kontrol(ENVIRON *csound)
  {
    //warn(csound, "MixerSetLevel::kontrol...\n");
    matrix[send][buss] = *kgain;
    //warn(csound, "MixerSetLevel::kontrol: send %d buss %d gain %f\n", send, buss, gain);
    return OK;
  }
  int deinit(void *csound)
  {
    if(busses.begin() != busses.end())
      {
	busses.clear();
      }
    if(matrix.begin() != matrix.end())
      {
	matrix.clear();
      }
    return OK;
  }
};

/**
 * kgain MixerGetLevel isend, ibuss
 *
 * Returns the gain of any signal route from a send to a bus.
 */
struct MixerGetLevel : public OpcodeBase<MixerGetLevel>
{
  // Outputs.
  MYFLT *kgain;
  // Inputs.
  MYFLT *isend;
  MYFLT *ibuss;
  // State.
  size_t send;
  size_t buss;
  int init(ENVIRON *csound)
  {
    send = static_cast<size_t>(*isend);
    buss = static_cast<size_t>(*ibuss);
    return OK;
  }
  int kontrol(ENVIRON *csound)
  {
    *kgain = matrix[send][buss];
    return OK;
  }
};
/**
 * MixerSend asignal, isend, ibus, ichannel
 *
 * Routes a signal from a send to a channel of a mixer bus.
 * The gain of the send is controlled by the previously set mixer level.
 */
struct MixerSend : public OpcodeBase<MixerSend>
{
  // No outputs.
  // Inputs.
  MYFLT *ainput;
  MYFLT *isend;
  MYFLT *ibuss;
  MYFLT *ichannel;
  // State.
  size_t send;
  size_t buss;
  size_t channel;
  size_t frames;
  MYFLT *busspointer;
  int init(ENVIRON *csound)
  {
    //warn(csound, "MixerSend::init...\n");
    send = static_cast<size_t>(*isend);
    buss = static_cast<size_t>(*ibuss);
    channel = static_cast<size_t>(*ichannel);
    frames = csound->GetKsmps(csound);
    busspointer = &busses[buss][channel].front();
    //warn(csound, "MixerSend::init: send %d buss %d channel %d frames %d busspointer 0x%x\n", send, buss, channel, frames, busspointer);
    return OK;
  }
  int audio(ENVIRON *csound)
  {
    //warn(csound, "MixerSend::audio...\n");
    MYFLT gain = matrix[send][buss];
    for(size_t i = 0; i < frames; i++)
      {
	busspointer[i] += (ainput[i] * gain);
      }
    //warn(csound, "MixerSend::audio: send %d buss %d gain %f busspointer 0x%x\n", send, buss, gain, busspointer);
    return OK;
  }
};

/**
 * asignal MixerReceive ibuss, ichannel
 *
 * Receives a signal from a channel of a bus.
 * Obviously, instruments receiving signals must be numbered higher 
 * than instruments sending those signals.
 */
struct MixerReceive : public OpcodeBase<MixerReceive>
{
  // Output.
  MYFLT *aoutput;
  // Inputs.
  MYFLT *ibuss;
  MYFLT *ichannel;
  // State.
  size_t buss;
  size_t channel;
  size_t frames;
  MYFLT *busspointer;
  int init(ENVIRON *csound)
  {
    //warn(csound, "MixerReceive::init...\n");
    buss = static_cast<size_t>(*ibuss);
    channel = static_cast<size_t>(*ichannel);
    frames = csound->GetKsmps(csound);
    busspointer = &busses[buss][channel].front();
    //warn(csound, "MixerReceive::init buss %d channel %d frames %d busspointer 0x%x\n", buss, channel, frames, busspointer);
    return OK;
  }
  int audio(ENVIRON *csound)
  {
    //warn(csound, "MixerReceive::audio...\n");
    for(size_t i = 0; i < frames; i++)
      {
	aoutput[i] = busspointer[i];
      }
    //warn(csound, "MixerReceive::audio aoutput 0x%x busspointer 0x%x\n", aoutput, busspointer);
    return OK;
  }
};

/**
 * MixerClear
 *
 * Clears all busses. Must be invoked after last MixerReceive.
 * You should probably use a highest-numbered instrument 
 * with an indefinite duration that invokes only this opcode.
 */
struct MixerClear : public OpcodeBase<MixerClear>
{
  // No output.
  // No input.
  // No state.
  int audio(ENVIRON *csound)
  {
    //warn(csound, "MixerClear::audio...\n")
    for(std::map<size_t, std::vector< std::vector<MYFLT> > >::iterator busi = busses.begin(); busi != busses.end(); ++busi)
      {
	for(std::vector< std::vector<MYFLT> >::iterator channeli = busi->second.begin(); channeli != busi->second.end(); ++channeli)
	  {
	    for(std::vector<MYFLT>::iterator framei = (*channeli).begin(); framei != (*channeli).end(); ++framei)
	      {
		*framei = 0;
	      }
	  }
      }
    //warn(csound, "MixerClear::audio\n")
    return OK;
  }
};

extern "C" 
{
  
  OENTRY  mixerOentries[] = { 
    {   
      "MixerSetLevel",         
      sizeof(MixerSetLevel),           
      3,  
      "",   
      "iik",      
      (SUBR)&MixerSetLevel::init_,        
      (SUBR)&MixerSetLevel::kontrol_,        
      0,                
      (SUBR)&MixerSetLevel::deinit_, 
    },
    {   
      "MixerGetLevel",         
      sizeof(MixerGetLevel),           
      3,  
      "k",   
      "ii",      
      (SUBR)&MixerGetLevel::init_,        
      (SUBR)&MixerGetLevel::kontrol_,        
      0,                
      (SUBR)&MixerGetLevel::deinit_, 
    },
    {   
      "MixerSend",         
      sizeof(MixerSend),           
      5,  
      "",   
      "aiii",      
      (SUBR)&MixerSend::init_,        
      0,                
      (SUBR)&MixerSend::audio_,        
      0, 
    },
    {   
      "MixerReceive",         
      sizeof(MixerReceive),           
      5,  
      "a",   
      "ii",      
      (SUBR)&MixerReceive::init_,        
      0,                
      (SUBR)&MixerReceive::audio_,        
      0,
    },
    {   
      "MixerClear",         
      sizeof(MixerClear),           
      4,  
      "",   
      "",      
      0,        
      0,        
      (SUBR)&MixerClear::audio_,        
      0, 
    },
  };
    
  /**
   * Called by Csound to obtain the size of
   * the table of OENTRY structures defined in this shared library.
   */
  PUBLIC int opcode_size()
  {
    return sizeof(mixerOentries);
  }

  /**
   * Called by Csound to obtain a pointer to
   * the table of OENTRY structures defined in this shared library.
   */
  PUBLIC OENTRY *opcode_init(ENVIRON *csound)
  {
    return mixerOentries;
  }
    
}; // END EXTERN C
