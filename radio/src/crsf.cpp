#include "crsf.h"
#include "opentx.h"
#include "trainer.h"

#include <algorithm>
#include <limits>

#ifdef __GNUC__
# pragma GCC diagnostic push
# pragma GCC diagnostic error "-Wswitch" // unfortunately the project uses -Wnoswitch
#endif

namespace CRSF {
    struct Servo {
        using Crsf = Trainer::Protocol::Crsf;
        using MesgType = Crsf::MesgType;
        
        static constexpr uint8_t mPauseCount{2}; // 2ms
        
        enum class State : uint8_t {Undefined, GotFCAddress, GotLength, Channels, Data, AwaitCRC, AwaitCRCAndDecode};
        
        static inline int16_t convertCrsfToPuls(uint16_t const value) {
            const int16_t centered = value - Crsf::CenterValue;
            return Trainer::clamp((centered * 5) / 8);
        }
        
        static inline void tick1ms() {
            if (mPauseCounter > 0) {
                --mPauseCounter;
            }
            else {
                mState = State::Undefined;
            }
        }
        
        static inline void process(const uint8_t b, const std::function<void()> f) {
            mPauseCounter = mPauseCount;
            switch(mState) { // enum-switch -> no default (intentional)
            case State::Undefined:
                csum.reset();
                if (b == Crsf::FcAddress) {
                    mState = State::GotFCAddress;
                }
                break;
            case State::GotFCAddress:
                if ((b > 2) && (b <= mData.size())) {
                    mLength = b - 2; // only payload (not including type and crc)
                    mIndex = 0;
                    mState = State::GotLength;
                }
                else {
                    mState = State::Undefined;
                }
                break;
            case State::GotLength:
                csum += b;
                if ((b == Crsf::FrametypeChannels) && (mLength == 22)) {
                    mState = State::Channels;
                }
                else {
                    mState = State::Data;
                }
                break;
            case State::Data:
                csum += b;
                if (++mIndex >= mLength) {
                    mState = State::AwaitCRC;
                }
                break;
            case State::Channels:
                csum += b;
                mData[mIndex] = b;
                if (++mIndex >= mLength) {
                    mState = State::AwaitCRCAndDecode;
                }
                break;
            case State::AwaitCRC:
                if (csum == b) {
                    // only channel data is decoded, nothing todo here
                } 
                mState = State::Undefined;
                break;
            case State::AwaitCRCAndDecode:
                if (csum == b) {
                    ++mPackages;
                    f();
                } 
                mState = State::Undefined;
                break;
            }            
        }        
        static inline void convert(int16_t* const pulses) {
            static_assert(MAX_TRAINER_CHANNELS == 16);
            pulses[0]  = (uint16_t) (((mData[0]    | mData[1] << 8))                 & Crsf::ValueMask);
            pulses[1]  = (uint16_t) ((mData[1]>>3  | mData[2] <<5)                   & Crsf::ValueMask);
            pulses[2]  = (uint16_t) ((mData[2]>>6  | mData[3] <<2 | mData[4]<<10)  	 & Crsf::ValueMask);
            pulses[3]  = (uint16_t) ((mData[4]>>1  | mData[5] <<7)                   & Crsf::ValueMask);
            pulses[4]  = (uint16_t) ((mData[5]>>4  | mData[6] <<4)                   & Crsf::ValueMask);
            pulses[5]  = (uint16_t) ((mData[6]>>7  | mData[7] <<1 | mData[8]<<9)   	 & Crsf::ValueMask);
            pulses[6]  = (uint16_t) ((mData[8]>>2  | mData[9] <<6)                   & Crsf::ValueMask);
            pulses[7]  = (uint16_t) ((mData[9]>>5  | mData[10]<<3)                   & Crsf::ValueMask);
            pulses[8]  = (uint16_t) ((mData[11]    | mData[12]<<8)                   & Crsf::ValueMask);
            pulses[9]  = (uint16_t) ((mData[12]>>3 | mData[13]<<5)                   & Crsf::ValueMask);
            pulses[10] = (uint16_t) ((mData[13]>>6 | mData[14]<<2 | mData[15]<<10) 	 & Crsf::ValueMask);
            pulses[11] = (uint16_t) ((mData[15]>>1 | mData[16]<<7)                   & Crsf::ValueMask);
            pulses[12] = (uint16_t) ((mData[16]>>4 | mData[17]<<4)                   & Crsf::ValueMask);
            pulses[13] = (uint16_t) ((mData[17]>>7 | mData[18]<<1 | mData[19]<<9)  	 & Crsf::ValueMask);
            pulses[14] = (uint16_t) ((mData[19]>>2 | mData[20]<<6)                   & Crsf::ValueMask);
            pulses[15] = (uint16_t) ((mData[20]>>5 | mData[21]<<3)                   & Crsf::ValueMask);
            
            for(size_t i = 0; i < MAX_TRAINER_CHANNELS; ++i) {
                pulses[i] = convertCrsfToPuls(pulses[i]);
            }
        }
    private:
        static CRC8 csum;
        static State mState;
        static MesgType mData; 
        static uint8_t mIndex;
        static uint8_t mLength;
        static uint16_t mPackages;
        static uint8_t mPauseCounter;
    };
    
    CRC8 Servo::csum;
    Servo::State Servo::mState{Servo::State::Undefined};
    Servo::MesgType Servo::mData; 
    uint8_t Servo::mIndex{0};
    uint8_t Servo::mLength{0};
    uint16_t Servo::mPackages{0};
    uint8_t Servo::mPauseCounter{Servo::mPauseCount}; // 2 ms
}

void crsfTrainerPauseCheck() {
#if !defined(SIMU)
# if defined(AUX_SERIAL) || defined(AUX2_SERIAL) || defined(TRAINER_MODULE_SBUS)
    if (hasSerialMode(UART_MODE_CRSF_TRAINER) >= 0) {
        CRSF::Servo::tick1ms();
        processCrsfInput();    
    }
# endif
#endif    
}

void processCrsfInput() {
#if !defined(SIMU)
  uint8_t rxchar;

  while (sbusAuxGetByte(&rxchar)) {
      CRSF::Servo::process(rxchar, [&](){
          CRSF::Servo::convert(ppmInput);
          ppmInputValidityTimer = PPM_IN_VALID_TIMEOUT;        
      });
  }
#endif
}

#ifdef __GNUC__
# pragma GCC diagnostic pop
#endif
