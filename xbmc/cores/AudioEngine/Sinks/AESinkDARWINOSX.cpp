/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "cores/AudioEngine/AEFactory.h"
#include "cores/AudioEngine/Sinks/AESinkDARWINOSX.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "cores/AudioEngine/Utils/AERingBuffer.h"
#include "cores/AudioEngine/Sinks/osx/CoreAudioHelpers.h"
#include "cores/AudioEngine/Sinks/osx/CoreAudioHardware.h"
#include "osx/DarwinUtils.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "threads/Condition.h"
#include "threads/CriticalSection.h"

#include <sstream>

#define CA_MAX_CHANNELS 8
static enum AEChannel CAChannelMap[CA_MAX_CHANNELS + 1] = {
  AE_CH_FL , AE_CH_FR , AE_CH_BL , AE_CH_BR , AE_CH_FC , AE_CH_LFE , AE_CH_SL , AE_CH_SR ,
  AE_CH_NULL
};

static bool HasSampleRate(const AESampleRateList &list, const unsigned int samplerate)
{
  for (size_t i = 0; i < list.size(); ++i)
  {
    if (list[i] == samplerate)
      return true;
  }
  return false;
}

static bool HasDataFormat(const AEDataFormatList &list, const enum AEDataFormat format)
{
  for (size_t i = 0; i < list.size(); ++i)
  {
    if (list[i] == format)
      return true;
  }
  return false;
}

typedef std::vector< std::pair<AudioDeviceID, CAEDeviceInfo> > CADeviceList;

static void EnumerateDevices(CADeviceList &list)
{
  CAEDeviceInfo device;

  std::string defaultDeviceName;
  CCoreAudioHardware::GetOutputDeviceName(defaultDeviceName);

  CoreAudioDeviceList deviceIDList;
  CCoreAudioHardware::GetOutputDevices(&deviceIDList);
  while (!deviceIDList.empty())
  {
    AudioDeviceID deviceID = deviceIDList.front();
    CCoreAudioDevice caDevice(deviceID);

    device.m_channels.Reset();
    device.m_dataFormats.clear();
    device.m_sampleRates.clear();

    device.m_deviceType = AE_DEVTYPE_PCM;
    device.m_deviceName = caDevice.GetName();
    device.m_displayName = device.m_deviceName;
    device.m_displayNameExtra = "";

    // flag indicating that passthroughformats where added throughout the stream enumeration
    bool hasPassthroughFormats = false;
    // the maximum number of channels found in the streams
    UInt32 numMaxChannels = 0;
    // the terminal type as reported by ca
    UInt32 caTerminalType = 0;
      
    bool isDigital = caDevice.IsDigital(caTerminalType);


    CLog::Log(LOGDEBUG, "EnumerateDevices:Device(%s)" , device.m_deviceName.c_str());
    AudioStreamIdList streams;
    if (caDevice.GetStreams(&streams))
    {
      for (AudioStreamIdList::iterator j = streams.begin(); j != streams.end(); ++j)
      {
        StreamFormatList streams;
        if (CCoreAudioStream::GetAvailablePhysicalFormats(*j, &streams))
        {
          for (StreamFormatList::iterator i = streams.begin(); i != streams.end(); ++i)
          {
            AudioStreamBasicDescription desc = i->mFormat;
            std::string formatString;
            CLog::Log(LOGDEBUG, "EnumerateDevices:Format(%s)" ,
                                StreamDescriptionToString(desc, formatString));

            // add stream format info
            switch (desc.mFormatID)
            {
              case kAudioFormatAC3:
              case kAudioFormat60958AC3:
                if (!HasDataFormat(device.m_dataFormats, AE_FMT_AC3))
                  device.m_dataFormats.push_back(AE_FMT_AC3);
                if (!HasDataFormat(device.m_dataFormats, AE_FMT_DTS))
                  device.m_dataFormats.push_back(AE_FMT_DTS);
                hasPassthroughFormats = true;
                isDigital = true;// sanity - those are always digital devices!
                break;
              default:
                AEDataFormat format = AE_FMT_INVALID;
                switch(desc.mBitsPerChannel)
                {
                  case 16:
                    if (desc.mFormatFlags & kAudioFormatFlagIsBigEndian)
                      format = AE_FMT_S16BE;
                    else
                    {
                      // if it is no digital stream per definition
                      // check if the device name suggests that it is digital
                      // (some hackintonshs are not so smart in announcing correct
                      // ca devices ...
                      if (!isDigital)
                      {
                        std::string devNameLower = device.m_deviceName;
                        StringUtils::ToLower(devNameLower);                       
                        isDigital = devNameLower.find("digital") != std::string::npos;
                      }

                      /* Passthrough is possible with a 2ch digital output */
                      if (desc.mChannelsPerFrame == 2 && isDigital)
                      {
                        if (desc.mSampleRate == 48000)
                        {
                          if (!HasDataFormat(device.m_dataFormats, AE_FMT_AC3))
                            device.m_dataFormats.push_back(AE_FMT_AC3);
                          if (!HasDataFormat(device.m_dataFormats, AE_FMT_DTS))
                            device.m_dataFormats.push_back(AE_FMT_DTS);
                          hasPassthroughFormats = true;
                        }
                        else if (desc.mSampleRate == 192000)
                        {
                          if (!HasDataFormat(device.m_dataFormats, AE_FMT_EAC3))
                            device.m_dataFormats.push_back(AE_FMT_EAC3);
                          hasPassthroughFormats = true;
                        }
                      }
                      format = AE_FMT_S16LE;
                    }
                    break;
                  case 24:
                    if (desc.mFormatFlags & kAudioFormatFlagIsBigEndian)
                      format = AE_FMT_S24BE3;
                    else
                      format = AE_FMT_S24LE3;
                    break;
                  case 32:
                    if (desc.mFormatFlags & kAudioFormatFlagIsFloat)
                      format = AE_FMT_FLOAT;
                    else
                    {
                      if (desc.mFormatFlags & kAudioFormatFlagIsBigEndian)
                        format = AE_FMT_S32BE;
                      else
                        format = AE_FMT_S32LE;
                    }
                    break;
                }
                
                if (numMaxChannels < desc.mChannelsPerFrame)
                  numMaxChannels = desc.mChannelsPerFrame;
                
                if (format != AE_FMT_INVALID && !HasDataFormat(device.m_dataFormats, format))
                  device.m_dataFormats.push_back(format);
                break;
            }

            // add channel info
            CAEChannelInfo channel_info;
            for (UInt32 chan = 0; chan < CA_MAX_CHANNELS && chan < desc.mChannelsPerFrame; ++chan)
            {
              if (!device.m_channels.HasChannel(CAChannelMap[chan]))
                device.m_channels += CAChannelMap[chan];
              channel_info += CAChannelMap[chan];
            }

            // add sample rate info
            // quirk devices which don't report a valid samplerate
            // add 44.1khz and 48khz in that case - user can use
            // the "fixed" audio config to force one of them
            if (desc.mSampleRate == 0)
            {
              CLog::Log(LOGWARNING, "%s no valid samplerate - adding 44.1khz and 48khz quirk", __FUNCTION__);
              desc.mSampleRate = 44100;
              if (!HasSampleRate(device.m_sampleRates, desc.mSampleRate))
                device.m_sampleRates.push_back(desc.mSampleRate);
              desc.mSampleRate = 48000;
            }

            if (!HasSampleRate(device.m_sampleRates, desc.mSampleRate))
              device.m_sampleRates.push_back(desc.mSampleRate);
          }
        }
      }
    }

    
    // flag indicating that the device name "sounds" like HDMI
    bool hasHdmiName = device.m_deviceName.find("HDMI") != std::string::npos;
    // flag indicating that the device name "sounds" like DisplayPort
    bool hasDisplayPortName = device.m_deviceName.find("DisplayPort") != std::string::npos;
    
    // decide the type of the device based on the discovered information
    // in the streams
    // device defaults to PCM (see start of the while loop)
    // it can be HDMI, DisplayPort or Optical
    // for all of those types it needs to support
    // passthroughformats and needs to be a digital port
    if (hasPassthroughFormats && isDigital)
    {
      // if the max number of channels was more then 2
      // this can be HDMI or DisplayPort or Thunderbolt
      if (numMaxChannels > 2)
      {
        // either the devicename suggests its HDMI
        // or CA reported the terminalType as HDMI
        if (hasHdmiName || caTerminalType == kIOAudioDeviceTransportTypeHdmi)
          device.m_deviceType = AE_DEVTYPE_HDMI;

        // either the devicename suggests its DisplayPort
        // or CA reported the terminalType as DisplayPort or Thunderbolt
        if (hasDisplayPortName || caTerminalType == kIOAudioDeviceTransportTypeDisplayPort || caTerminalType == kIOAudioDeviceTransportTypeThunderbolt)
          device.m_deviceType = AE_DEVTYPE_DP;
      }
      else// treat all other digital passthrough devices as optical
        device.m_deviceType = AE_DEVTYPE_IEC958;
    }

    // devicename based overwrites from former code - maybe FIXME at some point when we
    // are sure that the upper detection does its job in all[tm] use cases
    if (hasHdmiName)
      device.m_deviceType = AE_DEVTYPE_HDMI;
    if (hasDisplayPortName)
      device.m_deviceType = AE_DEVTYPE_DP;
    
    
    list.push_back(std::make_pair(deviceID, device));
    //in the first place of the list add the default device
    //with name "default" - if this is selected
    //we will output to whatever osx claims to be default
    //(allows transition from headphones to speaker and stuff
    //like that
    if(defaultDeviceName == device.m_deviceName)
    {
      device.m_deviceName = "default";
      device.m_displayName = "Default";
      list.insert(list.begin(), std::make_pair(deviceID, device));
    }

    deviceIDList.pop_front();
  }
}

/* static, threadsafe access to the device list */
static CADeviceList     s_devices;
static CCriticalSection s_devicesLock;

static void EnumerateDevices()
{
  CADeviceList devices;
  EnumerateDevices(devices);
  {
    CSingleLock lock(s_devicesLock);
    s_devices = devices;
  }
}

static CADeviceList GetDevices()
{
  CADeviceList list;
  {
    CSingleLock lock(s_devicesLock);
    list = s_devices;
  }
  return list;
}

OSStatus deviceChangedCB(AudioObjectID                       inObjectID,
                         UInt32                              inNumberAddresses,
                         const AudioObjectPropertyAddress    inAddresses[],
                         void*                               inClientData)
{
  CLog::Log(LOGDEBUG, "CoreAudio: audiodevicelist changed - reenumerating");
  CAEFactory::DeviceChange();
  CLog::Log(LOGDEBUG, "CoreAudio: audiodevicelist changed - done");
  return noErr;
}

void RegisterDeviceChangedCB(bool bRegister, void *ref)
{
  OSStatus ret = noErr;
  const AudioObjectPropertyAddress inAdr =
  {
    kAudioHardwarePropertyDevices,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };

  if (bRegister)
    ret = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &inAdr, deviceChangedCB, ref);
  else
    ret = AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &inAdr, deviceChangedCB, ref);

  if (ret != noErr)
    CLog::Log(LOGERROR, "CCoreAudioAE::Deinitialize - error %s a listener callback for device changes!", bRegister?"attaching":"removing");
}


////////////////////////////////////////////////////////////////////////////////////////////
CAESinkDARWINOSX::CAESinkDARWINOSX()
: m_latentFrames(0), m_outputBitstream(false), m_outputBuffer(NULL), m_buffer(NULL)
{
  // By default, kAudioHardwarePropertyRunLoop points at the process's main thread on SnowLeopard,
  // If your process lacks such a run loop, you can set kAudioHardwarePropertyRunLoop to NULL which
  // tells the HAL to run it's own thread for notifications (which was the default prior to SnowLeopard).
  // So tell the HAL to use its own thread for similar behavior under all supported versions of OSX.
  CFRunLoopRef theRunLoop = NULL;
  AudioObjectPropertyAddress theAddress = {
    kAudioHardwarePropertyRunLoop,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };
  OSStatus theError = AudioObjectSetPropertyData(kAudioObjectSystemObject,
                                                 &theAddress, 0, NULL, sizeof(CFRunLoopRef), &theRunLoop);
  if (theError != noErr)
  {
    CLog::Log(LOGERROR, "CCoreAudioAE::constructor: kAudioHardwarePropertyRunLoop error.");
  }
  RegisterDeviceChangedCB(true, this);
  m_started = false;
}

CAESinkDARWINOSX::~CAESinkDARWINOSX()
{
  RegisterDeviceChangedCB(false, this);
}

float ScoreStream(const AudioStreamBasicDescription &desc, const AEAudioFormat &format)
{
  float score = 0;
  if (format.m_dataFormat == AE_FMT_AC3 ||
      format.m_dataFormat == AE_FMT_DTS)
  {
    if (desc.mFormatID == kAudioFormat60958AC3 ||
        desc.mFormatID == 'IAC3' ||
        desc.mFormatID == kAudioFormatAC3)
    {
      if (desc.mSampleRate == format.m_sampleRate &&
          desc.mBitsPerChannel == CAEUtil::DataFormatToBits(format.m_dataFormat) &&
          desc.mChannelsPerFrame == format.m_channelLayout.Count())
      {
        // perfect match
        score = FLT_MAX;
      }
    }
  }
  if (format.m_dataFormat == AE_FMT_AC3 ||
      format.m_dataFormat == AE_FMT_DTS ||
      format.m_dataFormat == AE_FMT_EAC3)
  { // we should be able to bistreaming in PCM if the samplerate, bitdepth and channels match
    if (desc.mSampleRate       == format.m_sampleRate                            &&
        desc.mBitsPerChannel   == CAEUtil::DataFormatToBits(format.m_dataFormat) &&
        desc.mChannelsPerFrame == format.m_channelLayout.Count()                 &&
        desc.mFormatID         == kAudioFormatLinearPCM)
    {
      score = FLT_MAX / 2;
    }
  }
  else
  { // non-passthrough, whatever works is fine
    if (desc.mFormatID == kAudioFormatLinearPCM)
    {
      if (desc.mSampleRate == format.m_sampleRate)
        score += 10;
      else if (desc.mSampleRate > format.m_sampleRate)
        score += 1;
      if (desc.mChannelsPerFrame == format.m_channelLayout.Count())
        score += 5;
      else if (desc.mChannelsPerFrame > format.m_channelLayout.Count())
        score += 1;
      if (format.m_dataFormat == AE_FMT_FLOAT)
      { // for float, prefer the highest bitdepth we have
        if (desc.mBitsPerChannel >= 16)
          score += (desc.mBitsPerChannel / 8);
      }
      else
      {
        if (desc.mBitsPerChannel == CAEUtil::DataFormatToBits(format.m_dataFormat))
          score += 5;
        else if (desc.mBitsPerChannel == CAEUtil::DataFormatToBits(format.m_dataFormat))
          score += 1;
      }
    }
  }
  return score;
}

bool CAESinkDARWINOSX::Initialize(AEAudioFormat &format, std::string &device)
{
  AudioDeviceID deviceID = 0;
  CADeviceList devices = GetDevices();
  if (StringUtils::EqualsNoCase(device, "default"))
  {
    CCoreAudioHardware::GetOutputDeviceName(device);
    deviceID = CCoreAudioHardware::GetDefaultOutputDevice();
    CLog::Log(LOGNOTICE, "%s: Opening default device %s", __PRETTY_FUNCTION__, device.c_str());
  }
  else
  {
    for (size_t i = 0; i < devices.size(); i++)
    {
      if (device.find(devices[i].second.m_deviceName) != std::string::npos)
      {
        deviceID = devices[i].first;
        break;
      }
    }
  }

  if (!deviceID)
  {
    CLog::Log(LOGERROR, "%s: Unable to find device %s", __FUNCTION__, device.c_str());
    return false;
  }

  m_device.Open(deviceID);

  // Fetch a list of the streams defined by the output device
  AudioStreamIdList streams;
  m_device.GetStreams(&streams);

  CLog::Log(LOGDEBUG, "%s: Finding stream for format %s", __FUNCTION__, CAEUtil::DataFormatToStr(format.m_dataFormat));

  bool                        passthrough  = false;
  UInt32                      outputIndex  = 0;
  float                       outputScore  = 0;
  AudioStreamBasicDescription outputFormat = {0};
  AudioStreamID               outputStream = 0;

  /* The theory is to score based on
   1. Matching passthrough characteristics (i.e. passthrough flag)
   2. Matching sample rate.
   3. Matching bits per channel (or higher).
   4. Matching number of channels (or higher).
   */
  UInt32 index = 0;
  for (AudioStreamIdList::const_iterator i = streams.begin(); i != streams.end(); ++i)
  {
    // Probe physical formats
    StreamFormatList formats;
    CCoreAudioStream::GetAvailablePhysicalFormats(*i, &formats);
    for (StreamFormatList::const_iterator j = formats.begin(); j != formats.end(); ++j)
    {
      AudioStreamBasicDescription desc = j->mFormat;

      // quirk devices with invalid sample rate
      // assume that the user uses a fixed config
      // and knows what he is doing - so we use
      // the requested samplerate here
      if (desc.mSampleRate == 0)
        desc.mSampleRate = format.m_sampleRate;

      float score = ScoreStream(desc, format);

      std::string formatString;
      CLog::Log(LOGDEBUG, "%s: Physical Format: %s rated %f", __FUNCTION__, StreamDescriptionToString(desc, formatString), score);

      if (score > outputScore)
      {
        passthrough  = score > 1000;
        outputScore  = score;
        outputFormat = desc;
        outputStream = *i;
        outputIndex  = index;
      }
    }
    index++;
  }

  if (!outputFormat.mFormatID)
  {
    CLog::Log(LOGERROR, "%s, Unable to find suitable stream", __FUNCTION__);
    return false;
  }

  /* Update our AE format */
  format.m_sampleRate    = outputFormat.mSampleRate;
  if (outputFormat.mChannelsPerFrame != format.m_channelLayout.Count())
  { /* update the channel count.  We assume that they're layed out as given in CAChannelMap.
       if they're not, this is plain wrong */
    format.m_channelLayout.Reset();
    for (unsigned int i = 0; i < outputFormat.mChannelsPerFrame && i < CA_MAX_CHANNELS; i++)
      format.m_channelLayout += CAChannelMap[i];
  }

  m_outputBitstream   = passthrough && outputFormat.mFormatID == kAudioFormatLinearPCM;

  std::string formatString;
  CLog::Log(LOGDEBUG, "%s: Selected stream[%u] - id: 0x%04X, Physical Format: %s %s", __FUNCTION__, outputIndex, outputStream, StreamDescriptionToString(outputFormat, formatString), m_outputBitstream ? "bitstreamed passthrough" : "");

  SetHogMode(passthrough);

  // Configure the output stream object
  m_outputStream.Open(outputStream);

  AudioStreamBasicDescription virtualFormat, previousPhysicalFormat;
  m_outputStream.GetVirtualFormat(&virtualFormat);
  m_outputStream.GetPhysicalFormat(&previousPhysicalFormat);
  CLog::Log(LOGDEBUG, "%s: Previous Virtual Format: %s", __FUNCTION__, StreamDescriptionToString(virtualFormat, formatString));
  CLog::Log(LOGDEBUG, "%s: Previous Physical Format: %s", __FUNCTION__, StreamDescriptionToString(previousPhysicalFormat, formatString));

  m_outputStream.SetPhysicalFormat(&outputFormat); // Set the active format (the old one will be reverted when we close)
  m_outputStream.GetVirtualFormat(&virtualFormat);
  CLog::Log(LOGDEBUG, "%s: New Virtual Format: %s", __FUNCTION__, StreamDescriptionToString(virtualFormat, formatString));
  CLog::Log(LOGDEBUG, "%s: New Physical Format: %s", __FUNCTION__, StreamDescriptionToString(outputFormat, formatString));

  m_latentFrames = m_device.GetNumLatencyFrames();
  m_latentFrames += m_outputStream.GetNumLatencyFrames();

  /* TODO: Should we use the virtual format to determine our data format? */
  format.m_frameSize     = format.m_channelLayout.Count() * (CAEUtil::DataFormatToBits(format.m_dataFormat) >> 3);
  format.m_frames        = m_device.GetBufferSize();
  format.m_frameSamples  = format.m_frames * format.m_channelLayout.Count();

  if (m_outputBitstream)
  {
    m_outputBuffer = new int16_t[format.m_frameSamples];
    /* TODO: Do we need this? */
    m_device.SetNominalSampleRate(format.m_sampleRate);
  }

  unsigned int num_buffers = 4;
  m_buffer = new AERingBuffer(num_buffers * format.m_frames * format.m_frameSize);
  CLog::Log(LOGDEBUG, "%s: using buffer size: %u (%f ms)", __FUNCTION__, m_buffer->GetMaxSize(), (float)m_buffer->GetMaxSize() / (format.m_sampleRate * format.m_frameSize));

  m_format = format;
  if (passthrough)
    format.m_dataFormat = AE_FMT_S16NE;
  else
    format.m_dataFormat = AE_FMT_FLOAT;

  // Register for data request callbacks from the driver and start
  m_device.AddIOProc(renderCallback, this);
  m_device.Start();
  return true;
}

void CAESinkDARWINOSX::SetHogMode(bool on)
{
  // TODO: Auto hogging sets this for us. Figure out how/when to turn it off or use it
  // It appears that leaving this set will aslo restore the previous stream format when the
  // Application exits. If auto hogging is set and we try to set hog mode, we will deadlock
  // From the SDK docs: "If the AudioDevice is in a non-mixable mode, the HAL will automatically take hog mode on behalf of the first process to start an IOProc."

  // Lock down the device.  This MUST be done PRIOR to switching to a non-mixable format, if it is done at all
  // If it is attempted after the format change, there is a high likelihood of a deadlock
  // We may need to do this sooner to enable mix-disable (i.e. before setting the stream format)
  if (on)
  {
    // Auto-Hog does not always un-hog the device when changing back to a mixable mode.
    // Handle this on our own until it is fixed.
    CCoreAudioHardware::SetAutoHogMode(false);
    bool autoHog = CCoreAudioHardware::GetAutoHogMode();
    CLog::Log(LOGDEBUG, " CoreAudioRenderer::InitializeEncoded: "
              "Auto 'hog' mode is set to '%s'.", autoHog ? "On" : "Off");
    if (autoHog)
      return;
  }
  m_device.SetHogStatus(on);
  m_device.SetMixingSupport(!on);
}

void CAESinkDARWINOSX::Deinitialize()
{
  m_device.Stop();
  m_device.RemoveIOProc();

  m_outputStream.Close();
  m_device.Close();
  if (m_buffer)
  {
    delete m_buffer;
    m_buffer = NULL;
  }
  m_outputBitstream = false;

  delete[] m_outputBuffer;
  m_outputBuffer = NULL;

  m_started = false;
}

bool CAESinkDARWINOSX::IsCompatible(const AEAudioFormat &format, const std::string &device)
{
  return ((m_format.m_sampleRate    == format.m_sampleRate) &&
          (m_format.m_dataFormat    == format.m_dataFormat) &&
          (m_format.m_channelLayout == format.m_channelLayout));
}

double CAESinkDARWINOSX::GetDelay()
{
  if (m_buffer)
  {
    // Calculate the duration of the data in the cache
    double delay = (double)m_buffer->GetReadSize() / (double)m_format.m_frameSize;
    delay += (double)m_latentFrames;
    delay /= (double)m_format.m_sampleRate;
    return delay;
  }
  return 0.0;
}

double CAESinkDARWINOSX::GetCacheTotal()
{
  return (double)m_buffer->GetMaxSize() / (double)(m_format.m_frameSize * m_format.m_sampleRate);
}

CCriticalSection mutex;
XbmcThreads::ConditionVariable condVar;

unsigned int CAESinkDARWINOSX::AddPackets(uint8_t *data, unsigned int frames, bool hasAudio, bool blocking)
{
  if (m_buffer->GetWriteSize() < frames * m_format.m_frameSize)
  { // no space to write - wait for a bit
    CSingleLock lock(mutex);
    unsigned int timeout = 900 * frames / m_format.m_sampleRate;
    if (!m_started)
      timeout = 4500;

    // we are using a timer here for beeing sure for timeouts
    // condvar can be woken spuriously as signaled
    XbmcThreads::EndTime timer(timeout);
    condVar.wait(mutex, timeout);
    if (!m_started && timer.IsTimePast())
    {
      CLog::Log(LOGERROR, "%s engine didn't start in %d ms!", __FUNCTION__, timeout);
      return INT_MAX;    
    }
  }

  unsigned int write_frames = std::min(frames, m_buffer->GetWriteSize() / m_format.m_frameSize);
  if (write_frames)
    m_buffer->Write(data, write_frames * m_format.m_frameSize);

  return write_frames;
}

void CAESinkDARWINOSX::Drain()
{
  int bytes = m_buffer->GetReadSize();
  int totalBytes = bytes;
  int maxNumTimeouts = 3;
  unsigned int timeout = 900 * bytes / (m_format.m_sampleRate * m_format.m_frameSize);
  while (bytes && maxNumTimeouts > 0)
  {
    CSingleLock lock(mutex);
    XbmcThreads::EndTime timer(timeout);
    condVar.wait(mutex, timeout);

    bytes = m_buffer->GetReadSize();
    // if we timeout and don't
    // consum bytes - decrease maxNumTimeouts
    if (timer.IsTimePast() && bytes == totalBytes)
      maxNumTimeouts--;
    totalBytes = bytes;
  }
}

void CAESinkDARWINOSX::EnumerateDevicesEx(AEDeviceInfoList &list, bool force)
{
  EnumerateDevices();
  list.clear();
  for (CADeviceList::const_iterator i = s_devices.begin(); i != s_devices.end(); ++i)
    list.push_back(i->second);
}

inline void LogLevel(unsigned int got, unsigned int wanted)
{
  static unsigned int lastReported = INT_MAX;
  if (got != wanted)
  {
    if (got != lastReported)
    {
      CLog::Log(LOGWARNING, "DARWINOSX: %sflow (%u vs %u bytes)", got > wanted ? "over" : "under", got, wanted);
      lastReported = got;
    }    
  }
  else
    lastReported = INT_MAX; // indicate we were good at least once
}

OSStatus CAESinkDARWINOSX::renderCallback(AudioDeviceID inDevice, const AudioTimeStamp* inNow, const AudioBufferList* inInputData, const AudioTimeStamp* inInputTime, AudioBufferList* outOutputData, const AudioTimeStamp* inOutputTime, void* inClientData)
{
  CAESinkDARWINOSX *sink = (CAESinkDARWINOSX*)inClientData;

  sink->m_started = true;
  for (unsigned int i = 0; i < outOutputData->mNumberBuffers; i++)
  {
    if (sink->m_outputBitstream)
    {
      /* HACK for bitstreaming AC3/DTS via PCM.
       We reverse the float->S16LE conversion done in the stream or device */
      static const float mul = 1.0f / (INT16_MAX + 1);

      unsigned int wanted = std::min(outOutputData->mBuffers[i].mDataByteSize / sizeof(float), (size_t)sink->m_format.m_frameSamples)  * sizeof(int16_t);
      if (wanted <= sink->m_buffer->GetReadSize())
      {
        sink->m_buffer->Read((unsigned char *)sink->m_outputBuffer, wanted);
        int16_t *src = sink->m_outputBuffer;
        float  *dest = (float*)outOutputData->mBuffers[i].mData;
        for (unsigned int i = 0; i < wanted / 2; i++)
          *dest++ = *src++ * mul;
      }
    }
    else
    {
      /* buffers appear to come from CA already zero'd, so just copy what is wanted */
      unsigned int wanted = outOutputData->mBuffers[i].mDataByteSize;
      unsigned int bytes = std::min(sink->m_buffer->GetReadSize(), wanted);
      sink->m_buffer->Read((unsigned char*)outOutputData->mBuffers[i].mData, bytes);
      LogLevel(bytes, wanted);
    }

    // tell the sink we're good for more data
    condVar.notifyAll();
  }
  return noErr;
}
