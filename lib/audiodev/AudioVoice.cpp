#include "AudioVoice.hpp"
#include "AudioVoiceEngine.hpp"
#include "logvisor/logvisor.hpp"

namespace boo
{
static logvisor::Module Log("boo::AudioVoice");

static AudioMatrixMono DefaultMonoMtx;
static AudioMatrixStereo DefaultStereoMtx;

AudioVoice::AudioVoice(BaseAudioVoiceEngine& root,
                       IAudioVoiceCallback* cb, bool dynamicRate)
: m_root(root), m_cb(cb), m_dynamicRate(dynamicRate) {}

AudioVoice::~AudioVoice()
{
    unbindVoice();
    soxr_delete(m_src);
}

void AudioVoice::_setPitchRatio(double ratio, bool slew)
{
    if (m_dynamicRate)
    {
        soxr_error_t err = soxr_set_io_ratio(m_src, ratio * m_sampleRateIn / m_sampleRateOut, slew ? m_root.m_5msFrames : 0);
        if (err)
        {
            Log.report(logvisor::Fatal, "unable to set resampler rate: %s", soxr_strerror(err));
            m_setPitchRatio = false;
            return;
        }
    }
    m_setPitchRatio = false;
}

void AudioVoice::_midUpdate()
{
    if (m_resetSampleRate)
        _resetSampleRate(m_deferredSampleRate);
    if (m_setPitchRatio)
        _setPitchRatio(m_pitchRatio, m_slew);
}

void AudioVoice::setPitchRatio(double ratio, bool slew)
{
    m_setPitchRatio = true;
    m_pitchRatio = ratio;
    m_slew = slew;
}

void AudioVoice::resetSampleRate(double sampleRate)
{
    m_resetSampleRate = true;
    m_deferredSampleRate = sampleRate;
}

void AudioVoice::start()
{
    m_running = true;
}

void AudioVoice::stop()
{
    m_running = false;
}

void AudioVoice::unbindVoice()
{
    if (m_bound)
    {
        m_root._unbindFrom(m_parentIt);
        m_bound = false;
    }
}

AudioVoiceMono::AudioVoiceMono(BaseAudioVoiceEngine& root, IAudioVoiceCallback* cb,
                               double sampleRate, bool dynamicRate)
: AudioVoice(root, cb, dynamicRate)
{
    _resetSampleRate(sampleRate);
}

void AudioVoiceMono::_resetSampleRate(double sampleRate)
{
    soxr_delete(m_src);

    double rateOut = m_root.mixInfo().m_sampleRate;
    soxr_datatype_t formatOut = m_root.mixInfo().m_sampleFormat;
    soxr_io_spec_t ioSpec = soxr_io_spec(SOXR_INT16_I, formatOut);
    soxr_quality_spec_t qSpec = soxr_quality_spec(SOXR_20_BITQ, m_dynamicRate ? SOXR_VR : 0);

    soxr_error_t err;
    m_src = soxr_create(sampleRate, rateOut, 1,
                        &err, &ioSpec, &qSpec, nullptr);

    if (err)
    {
        Log.report(logvisor::Fatal, "unable to create soxr resampler: %s", soxr_strerror(err));
        m_resetSampleRate = false;
        return;
    }

    m_sampleRateIn = sampleRate;
    m_sampleRateOut = rateOut;
    soxr_set_input_fn(m_src, soxr_input_fn_t(SRCCallback), this, 0);
    _setPitchRatio(m_pitchRatio, false);
    m_resetSampleRate = false;
}

size_t AudioVoiceMono::SRCCallback(AudioVoiceMono* ctx, int16_t** data, size_t frames)
{
    std::vector<int16_t>& scratchIn = ctx->m_root.m_scratchIn;
    if (scratchIn.size() < frames)
        scratchIn.resize(frames);
    *data = scratchIn.data();
    if (ctx->m_silentOut)
    {
        memset(*data, 0, frames * 2);
        return frames;
    }
    else
        return ctx->m_cb->supplyAudio(*ctx, frames, scratchIn.data());
}

size_t AudioVoiceMono::pumpAndMix16(size_t frames)
{
    std::vector<int16_t>& scratch16Pre = m_root.m_scratch16Pre;
    if (scratch16Pre.size() < frames)
        scratch16Pre.resize(frames);

    std::vector<int16_t>& scratch16Post = m_root.m_scratch16Post;
    if (scratch16Post.size() < frames)
        scratch16Post.resize(frames);

    double dt = frames / m_sampleRateOut;
    m_cb->preSupplyAudio(*this, dt);
    _midUpdate();
    size_t oDone = soxr_output(m_src, scratch16Pre.data(), frames);

    if (oDone)
    {
        if (m_sendMatrices.size())
        {
            for (auto& mtx : m_sendMatrices)
            {
                AudioSubmix& smx = *reinterpret_cast<AudioSubmix*>(mtx.first);
                m_cb->routeAudio(oDone, 1, dt, smx.m_busId, scratch16Pre.data(), scratch16Post.data());
                mtx.second.mixMonoSampleData(m_root.m_mixInfo, scratch16Post.data(), smx._getMergeBuf16(oDone), oDone);
            }
        }
        else
        {
            AudioSubmix& smx = reinterpret_cast<AudioSubmix&>(m_root.m_mainSubmix);
            m_cb->routeAudio(oDone, 1, dt, m_root.m_mainSubmix.m_busId, scratch16Pre.data(), scratch16Post.data());
            DefaultMonoMtx.mixMonoSampleData(m_root.m_mixInfo, scratch16Post.data(), smx._getMergeBuf16(oDone), oDone);
        }
    }

    return oDone;
}

size_t AudioVoiceMono::pumpAndMix32(size_t frames)
{
    std::vector<int32_t>& scratch32Pre = m_root.m_scratch32Pre;
    if (scratch32Pre.size() < frames)
        scratch32Pre.resize(frames);

    std::vector<int32_t>& scratch32Post = m_root.m_scratch32Post;
    if (scratch32Post.size() < frames)
        scratch32Post.resize(frames);

    double dt = frames / m_sampleRateOut;
    m_cb->preSupplyAudio(*this, dt);
    _midUpdate();
    size_t oDone = soxr_output(m_src, scratch32Pre.data(), frames);

    if (oDone)
    {
        if (m_sendMatrices.size())
        {
            for (auto& mtx : m_sendMatrices)
            {
                AudioSubmix& smx = *reinterpret_cast<AudioSubmix*>(mtx.first);
                m_cb->routeAudio(oDone, 1, dt, smx.m_busId, scratch32Pre.data(), scratch32Post.data());
                mtx.second.mixMonoSampleData(m_root.m_mixInfo, scratch32Post.data(), smx._getMergeBuf32(oDone), oDone);
            }
        }
        else
        {
            AudioSubmix& smx = reinterpret_cast<AudioSubmix&>(m_root.m_mainSubmix);
            m_cb->routeAudio(oDone, 1, dt, m_root.m_mainSubmix.m_busId, scratch32Pre.data(), scratch32Post.data());
            DefaultMonoMtx.mixMonoSampleData(m_root.m_mixInfo, scratch32Post.data(), smx._getMergeBuf32(oDone), oDone);
        }
    }

    return oDone;
}

size_t AudioVoiceMono::pumpAndMixFlt(size_t frames)
{
    std::vector<float>& scratchFltPre = m_root.m_scratchFltPre;
    if (scratchFltPre.size() < frames)
        scratchFltPre.resize(frames + 2);

    std::vector<float>& scratchFltPost = m_root.m_scratchFltPost;
    if (scratchFltPost.size() < frames)
        scratchFltPost.resize(frames + 2);

    double dt = frames / m_sampleRateOut;
    m_cb->preSupplyAudio(*this, dt);
    _midUpdate();
    size_t oDone = soxr_output(m_src, scratchFltPre.data(), frames);

    if (oDone)
    {
        if (m_sendMatrices.size())
        {
            for (auto& mtx : m_sendMatrices)
            {
                AudioSubmix& smx = *reinterpret_cast<AudioSubmix*>(mtx.first);
                m_cb->routeAudio(oDone, 1, dt, smx.m_busId, scratchFltPre.data(), scratchFltPost.data());
                mtx.second.mixMonoSampleData(m_root.m_mixInfo, scratchFltPost.data(), smx._getMergeBufFlt(oDone), oDone);
            }
        }
        else
        {
            AudioSubmix& smx = reinterpret_cast<AudioSubmix&>(m_root.m_mainSubmix);
            m_cb->routeAudio(oDone, 1, dt, m_root.m_mainSubmix.m_busId, scratchFltPre.data(), scratchFltPost.data());
            DefaultMonoMtx.mixMonoSampleData(m_root.m_mixInfo, scratchFltPost.data(), smx._getMergeBufFlt(oDone), oDone);
        }
    }

    return oDone;
}

void AudioVoiceMono::resetChannelLevels()
{
    m_root.m_submixesDirty = true;
    m_sendMatrices.clear();
}

void AudioVoiceMono::setMonoChannelLevels(IAudioSubmix* submix, const float coefs[8], bool slew)
{
    if (!submix)
        submix = &m_root.m_mainSubmix;

    auto search = m_sendMatrices.find(submix);
    if (search == m_sendMatrices.cend())
        search = m_sendMatrices.emplace(submix, AudioMatrixMono{}).first;
    search->second.setMatrixCoefficients(coefs, slew ? m_root.m_5msFrames : 0);
}

void AudioVoiceMono::setStereoChannelLevels(IAudioSubmix* submix, const float coefs[8][2], bool slew)
{
    float newCoefs[8] =
    {
        coefs[0][0],
        coefs[1][0],
        coefs[2][0],
        coefs[3][0],
        coefs[4][0],
        coefs[5][0],
        coefs[6][0],
        coefs[7][0]
    };

    if (!submix)
        submix = &m_root.m_mainSubmix;

    auto search = m_sendMatrices.find(submix);
    if (search == m_sendMatrices.cend())
        search = m_sendMatrices.emplace(submix, AudioMatrixMono{}).first;
    search->second.setMatrixCoefficients(newCoefs, slew ? m_root.m_5msFrames : 0);
}

AudioVoiceStereo::AudioVoiceStereo(BaseAudioVoiceEngine& root, IAudioVoiceCallback* cb,
                                   double sampleRate, bool dynamicRate)
: AudioVoice(root, cb, dynamicRate)
{
    _resetSampleRate(sampleRate);
}

void AudioVoiceStereo::_resetSampleRate(double sampleRate)
{
    soxr_delete(m_src);

    double rateOut = m_root.mixInfo().m_sampleRate;
    soxr_datatype_t formatOut = m_root.mixInfo().m_sampleFormat;
    soxr_io_spec_t ioSpec = soxr_io_spec(SOXR_INT16_I, formatOut);
    soxr_quality_spec_t qSpec = soxr_quality_spec(SOXR_20_BITQ, m_dynamicRate ? SOXR_VR : 0);

    soxr_error_t err;
    m_src = soxr_create(sampleRate, rateOut, 2,
                        &err, &ioSpec, &qSpec, nullptr);

    if (!m_src)
    {
        Log.report(logvisor::Fatal, "unable to create soxr resampler: %s", soxr_strerror(err));
        m_resetSampleRate = false;
        return;
    }

    m_sampleRateIn = sampleRate;
    m_sampleRateOut = rateOut;
    soxr_set_input_fn(m_src, soxr_input_fn_t(SRCCallback), this, 0);
    _setPitchRatio(m_pitchRatio, false);
    m_resetSampleRate = false;
}

size_t AudioVoiceStereo::SRCCallback(AudioVoiceStereo* ctx, int16_t** data, size_t frames)
{
    std::vector<int16_t>& scratchIn = ctx->m_root.m_scratchIn;
    size_t samples = frames * 2;
    if (scratchIn.size() < samples)
        scratchIn.resize(samples);
    *data = scratchIn.data();
    if (ctx->m_silentOut)
    {
        memset(*data, 0, samples * 2);
        return frames;
    }
    else
        return ctx->m_cb->supplyAudio(*ctx, frames, scratchIn.data());
}

size_t AudioVoiceStereo::pumpAndMix16(size_t frames)
{
    size_t samples = frames * 2;

    std::vector<int16_t>& scratch16Pre = m_root.m_scratch16Pre;
    if (scratch16Pre.size() < samples)
        scratch16Pre.resize(samples);

    std::vector<int16_t>& scratch16Post = m_root.m_scratch16Post;
    if (scratch16Post.size() < samples)
        scratch16Post.resize(samples);

    double dt = frames / m_sampleRateOut;
    m_cb->preSupplyAudio(*this, dt);
    _midUpdate();
    size_t oDone = soxr_output(m_src, scratch16Pre.data(), frames);

    if (oDone)
    {
        if (m_sendMatrices.size())
        {
            for (auto& mtx : m_sendMatrices)
            {
                AudioSubmix& smx = *reinterpret_cast<AudioSubmix*>(mtx.first);
                m_cb->routeAudio(oDone, 2, dt, smx.m_busId, scratch16Pre.data(), scratch16Post.data());
                mtx.second.mixStereoSampleData(m_root.m_mixInfo, scratch16Post.data(), smx._getMergeBuf16(oDone), oDone);
            }
        }
        else
        {
            AudioSubmix& smx = reinterpret_cast<AudioSubmix&>(m_root.m_mainSubmix);
            m_cb->routeAudio(oDone, 2, dt, m_root.m_mainSubmix.m_busId, scratch16Pre.data(), scratch16Post.data());
            DefaultStereoMtx.mixStereoSampleData(m_root.m_mixInfo, scratch16Post.data(), smx._getMergeBuf16(oDone), oDone);
        }
    }

    return oDone;
}

size_t AudioVoiceStereo::pumpAndMix32(size_t frames)
{
    size_t samples = frames * 2;

    std::vector<int32_t>& scratch32Pre = m_root.m_scratch32Pre;
    if (scratch32Pre.size() < samples)
        scratch32Pre.resize(samples);

    std::vector<int32_t>& scratch32Post = m_root.m_scratch32Post;
    if (scratch32Post.size() < samples)
        scratch32Post.resize(samples);

    double dt = frames / m_sampleRateOut;
    m_cb->preSupplyAudio(*this, dt);
    _midUpdate();
    size_t oDone = soxr_output(m_src, scratch32Pre.data(), frames);

    if (oDone)
    {
        if (m_sendMatrices.size())
        {
            for (auto& mtx : m_sendMatrices)
            {
                AudioSubmix& smx = *reinterpret_cast<AudioSubmix*>(mtx.first);
                m_cb->routeAudio(oDone, 2, dt, smx.m_busId, scratch32Pre.data(), scratch32Post.data());
                mtx.second.mixStereoSampleData(m_root.m_mixInfo, scratch32Post.data(), smx._getMergeBuf32(oDone), oDone);
            }
        }
        else
        {
            AudioSubmix& smx = reinterpret_cast<AudioSubmix&>(m_root.m_mainSubmix);
            m_cb->routeAudio(oDone, 2, dt, m_root.m_mainSubmix.m_busId, scratch32Pre.data(), scratch32Post.data());
            DefaultStereoMtx.mixStereoSampleData(m_root.m_mixInfo, scratch32Post.data(), smx._getMergeBuf32(oDone), oDone);
        }
    }

    return oDone;
}

size_t AudioVoiceStereo::pumpAndMixFlt(size_t frames)
{
    size_t samples = frames * 2;

    std::vector<float>& scratchFltPre = m_root.m_scratchFltPre;
    if (scratchFltPre.size() < samples)
        scratchFltPre.resize(samples + 4);

    std::vector<float>& scratchFltPost = m_root.m_scratchFltPost;
    if (scratchFltPost.size() < samples)
        scratchFltPost.resize(samples + 4);

    double dt = frames / m_sampleRateOut;
    m_cb->preSupplyAudio(*this, dt);
    _midUpdate();
    size_t oDone = soxr_output(m_src, scratchFltPre.data(), frames);

    if (oDone)
    {
        if (m_sendMatrices.size())
        {
            for (auto& mtx : m_sendMatrices)
            {
                AudioSubmix& smx = *reinterpret_cast<AudioSubmix*>(mtx.first);
                m_cb->routeAudio(oDone, 2, dt, smx.m_busId, scratchFltPre.data(), scratchFltPost.data());
                mtx.second.mixStereoSampleData(m_root.m_mixInfo, scratchFltPost.data(), smx._getMergeBufFlt(oDone), oDone);
            }
        }
        else
        {
            AudioSubmix& smx = reinterpret_cast<AudioSubmix&>(m_root.m_mainSubmix);
            m_cb->routeAudio(oDone, 2, dt, m_root.m_mainSubmix.m_busId, scratchFltPre.data(), scratchFltPost.data());
            DefaultStereoMtx.mixStereoSampleData(m_root.m_mixInfo, scratchFltPost.data(), smx._getMergeBufFlt(oDone), oDone);
        }
    }

    return oDone;
}

void AudioVoiceStereo::resetChannelLevels()
{
    m_root.m_submixesDirty = true;
    m_sendMatrices.clear();
}

void AudioVoiceStereo::setMonoChannelLevels(IAudioSubmix* submix, const float coefs[8], bool slew)
{
    float newCoefs[8][2] =
    {
        {coefs[0], coefs[0]},
        {coefs[1], coefs[1]},
        {coefs[2], coefs[2]},
        {coefs[3], coefs[3]},
        {coefs[4], coefs[4]},
        {coefs[5], coefs[5]},
        {coefs[6], coefs[6]},
        {coefs[7], coefs[7]}
    };

    if (!submix)
        submix = &m_root.m_mainSubmix;

    auto search = m_sendMatrices.find(submix);
    if (search == m_sendMatrices.cend())
        search = m_sendMatrices.emplace(submix, AudioMatrixStereo{}).first;
    search->second.setMatrixCoefficients(newCoefs, slew ? m_root.m_5msFrames : 0);
}

void AudioVoiceStereo::setStereoChannelLevels(IAudioSubmix* submix, const float coefs[8][2], bool slew)
{
    if (!submix)
        submix = &m_root.m_mainSubmix;

    auto search = m_sendMatrices.find(submix);
    if (search == m_sendMatrices.cend())
        search = m_sendMatrices.emplace(submix, AudioMatrixStereo{}).first;
    search->second.setMatrixCoefficients(coefs, slew ? m_root.m_5msFrames : 0);
}

}
