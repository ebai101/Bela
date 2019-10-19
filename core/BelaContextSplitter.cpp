#include <BelaContextSplitter.h>
#include <stddef.h>

int BelaContextSplitter::setup(unsigned int in, unsigned int out, const BelaContext* context)
{
	if((in != 1 && out != 1) || (in < 1 || out < 1) || !context)
	{
		return -1;
	}
	(1 == in) ? direction = kOut : direction = kIn;

	InternalBelaContext ctx = *(InternalBelaContext*)context;
	resizeContext(ctx, in, out);
	outContexts.resize(out, ctx);
	for(auto &ctx : outContexts)
		contextAllocate(&ctx);
	inCount = 0;
	outCount = 0;
	inLength = in;
	outLength = out;

	offsets[kAudioIn].frames = offsetof(BelaContext, audioFrames);
	offsets[kAudioIn].channels = offsetof(BelaContext, audioInChannels);
	offsets[kAudioIn].data = offsetof(BelaContext, audioIn);

	offsets[kAudioOut].frames = offsetof(BelaContext, audioFrames);
	offsets[kAudioOut].channels = offsetof(BelaContext, audioOutChannels);
	offsets[kAudioOut].data = offsetof(BelaContext, audioOut);

	offsets[kAnalogIn].frames = offsetof(BelaContext, analogFrames);
	offsets[kAnalogIn].channels = offsetof(BelaContext, analogInChannels);
	offsets[kAnalogIn].data = offsetof(BelaContext, analogIn);

	offsets[kAnalogOut].frames = offsetof(BelaContext, analogFrames);
	offsets[kAnalogOut].channels = offsetof(BelaContext, analogOutChannels);
	offsets[kAnalogOut].data = offsetof(BelaContext, analogOut);
	return 0;
}

uint32_t BelaContextSplitter::getFramesForStream(const struct streamOffsets& o, const InternalBelaContext* context)
{
	return *(decltype(context->audioFrames)*)((char*)context + o.frames);
}

uint32_t BelaContextSplitter::getChannelsForStream(const struct streamOffsets& o, const InternalBelaContext* context)
{
	return *(decltype(context->audioOutChannels)*)((char*)context + o.channels);
}

float* BelaContextSplitter::getDataForStream(const struct streamOffsets& o, const InternalBelaContext* context)
{
	return *(decltype(context->audioOut)*)((char*)context + o.data);
}

int BelaContextSplitter::push(const BelaContext* inContext)
{
	const InternalBelaContext* sourceCtx = (InternalBelaContext*)inContext;
	if(inCount > inLength)
		return -1;
	for(auto& ctx : outContexts)
	{
		//TODO: digitals are missing
		for(unsigned int n = 0; n < kNumStreams; ++n)
		{
			const struct streamOffsets& o = offsets[n];
			unsigned int channels = getChannelsForStream(o, sourceCtx);
			float* source;
			float* dest;
			unsigned int sourceStartFrame;
			unsigned int destStartFrame;
			unsigned int sourceFrames;
			unsigned int destFrames;
			if(kIn == direction)
			{
				if(0 == inCount)
				{
					ctx.audioFramesElapsed = sourceCtx->audioFramesElapsed;
				}
				sourceStartFrame = 0;
				// TODO: line below assumes all input contexts have the same length
				destStartFrame = inCount * getFramesForStream(o, sourceCtx);
			} else {
				///cActx.audioFramesElapsed =TODO
				sourceStartFrame = inCount * getFramesForStream(o, &ctx);
				destStartFrame = 0;
			}
			source = getDataForStream(o, sourceCtx);
			dest = getDataForStream(o, &ctx);
			sourceFrames = getFramesForStream(o, sourceCtx);
			destFrames = getFramesForStream(o, &ctx);
			stackFrames(sourceCtx->flags & BELA_FLAG_INTERLEAVED,
					source, dest, channels,
					sourceStartFrame, destStartFrame,
					sourceFrames, destFrames);
		}
	}
	++inCount;
	if(inCount == inLength)
	{
		outCount = outLength;
		inCount = 0;
	}
	return inCount;
}

BelaContext* BelaContextSplitter::pop()
{
	if(outCount)
	{
		return (BelaContext*)&outContexts[outLength - outCount--];
	}
	return nullptr;
}

void BelaContextSplitter::resizeContext(InternalBelaContext& context, size_t in, size_t out)
{
	context.audioFrames *= in / out;
	context.analogFrames *= in / out;
	context.digitalFrames *= in / out;
	// todo: make each of the below aligned to 128-bit boundaries
	context.audioIn = new float[context.audioFrames * context.audioInChannels];
	context.audioOut = new float[context.audioFrames * context.audioOutChannels];
	context.analogIn = new float[context.analogFrames * context.analogInChannels];
	context.analogOut = new float[context.analogFrames * context.analogOutChannels];;
	context.digital = new uint32_t[context.digitalFrames];;
	context.multiplexerAnalogIn = nullptr; // TODO
}

void BelaContextSplitter::stackFrames(bool interleaved, const float* source, float* dest, unsigned int channels, unsigned int sourceStartFrame, unsigned int destStartFrame, unsigned int sourceFrames, unsigned int destFrames)
{
	for(unsigned int sn = sourceStartFrame, dn = destStartFrame;
			sn < sourceStartFrame + sourceFrames; ++dn, ++sn)
	{
		for(unsigned int c = 0; c < channels; ++c)
		{
			if(interleaved)
			{
				dest[dn * channels + c] = source[sn * channels + c];
			} else {
				dest[destFrames * c + dn] = source[sourceFrames * c + sn];
			}
		}
	}
}

void BelaContextSplitter::cleanup()
{
	for(auto& context : outContexts)
	{
		delete [] context.audioIn;
		delete [] context.audioOut;
		delete [] context.analogIn;
		delete [] context.analogOut;
		delete [] context.digital;
	}
	outContexts.clear();
}

#include <string.h>
static bool arrayEqual(const void* data1, const void* data2, size_t size)
{
	for(size_t n = 0; n < size; ++n)
		if(((const char*)data1)[n] != ((const char*)data2)[n]) 
			return false;
	return true;
}

#define TEST(val) if(!(val)) {\
		printf("In %s at line %d, test failed\n", __FILE__, __LINE__);\
		return false;\
	}


bool BelaContextSplitter::contextEqual(const InternalBelaContext* ctx1, const InternalBelaContext* ctx2)
{
	InternalBelaContext ctxc1 = *(InternalBelaContext*)ctx1;
	InternalBelaContext ctxc2 = *(InternalBelaContext*)ctx1;
	for(auto ctx : {&ctxc1, &ctxc2})
	{
		auto& c = *ctx;
		// ignores
		c.audioFramesElapsed = 0;
		//ignore arrays
		/*
		memset((void*)c.audioIn, 0, sizeof(c.audioIn[0])*c.audioFrames*c.audioInChannels);
		memset((void*)c.audioOut, 0, sizeof(c.audioOut[0])*c.audioFrames*c.audioOutChannels);
		memset((void*)c.analogIn, 0, sizeof(c.analogIn[0])*c.analogFrames*c.analogInChannels);
		memset((void*)c.analogOut, 0, sizeof(c.analogOut[0])*c.analogFrames*c.analogOutChannels);
		memset((void*)c.digital, 0, sizeof(c.digital[0])*c.digitalFrames);
		*/
		c.audioIn = 0;
		c.audioOut = 0;
		c.analogIn = 0;
		c.analogOut = 0;
		c.digital = 0;
	}
	// compare values
	TEST(arrayEqual((const void*) &ctxc1, (const void*) &ctxc2, sizeof(ctxc1)));
	// now check the arrays. We rolled over them earlier, so let's look at
	// the source again
	TEST(arrayEqual((const void*) ctx1->audioIn, (const void*) ctx2->audioIn, 
		ctx1->audioFrames * ctx1->audioInChannels * sizeof(ctx1->audioIn[0])));
	TEST(arrayEqual((const void*) ctx1->audioOut, (const void*) ctx2->audioOut, 
		ctx1->audioFrames * ctx1->audioOutChannels * sizeof(ctx1->audioOut[0])));
	TEST(arrayEqual((const void*) ctx1->analogIn, (const void*) ctx2->analogIn, 
		ctx1->analogInChannels * sizeof(ctx1->analogIn[0])));
	TEST(arrayEqual((const void*) ctx1->analogOut, (const void*) ctx2->analogOut, 
		ctx1->analogOutChannels * sizeof(ctx1->analogOut[0])));
	TEST(arrayEqual((const void*) ctx1->digital, (const void*) ctx2->digital,
		ctx1->digitalFrames * sizeof(ctx1->digital[0])));
	return true;
}
void BelaContextSplitter::contextAllocate(InternalBelaContext* ctx)
{
	ctx->audioIn = new float[ctx->audioFrames * ctx->audioInChannels];
	ctx->audioOut = new float[ctx->audioFrames * ctx->audioOutChannels];
	ctx->analogIn = new float[ctx->analogFrames * ctx->analogInChannels];
	ctx->analogOut = new float[ctx->analogFrames * ctx->analogOutChannels];
	ctx->digital = new uint32_t[ctx->digitalFrames];
}
void BelaContextSplitter::contextCopy(const InternalBelaContext* csrc, InternalBelaContext* cdst)
{
	const InternalBelaContext* src = (InternalBelaContext*)csrc;
	InternalBelaContext* dst = (InternalBelaContext*)cdst;
	memcpy((void*)dst, (void*)src, sizeof(InternalBelaContext));
	contextAllocate(dst);
	memcpy((void*)dst->audioIn, (void*)src->audioIn, sizeof(src->audioIn[0])*src->audioFrames*src->audioInChannels);
	memcpy((void*)dst->audioOut, (void*)src->audioOut, sizeof(src->audioOut[0])*src->audioFrames*src->audioOutChannels);
	memcpy((void*)dst->analogIn, (void*)src->analogIn, sizeof(src->analogIn[0])*src->analogFrames*src->analogInChannels);
	memcpy((void*)dst->analogOut, (void*)src->analogOut, sizeof(src->analogOut[0])*src->analogFrames*src->analogOutChannels);
	memcpy((void*)dst->digital, (void*)src->digital, sizeof(src->digital[0])*src->digitalFrames);

	return;
}

#undef NDEBUG
#include <assert.h>
bool BelaContextSplitter::test()
{
	std::vector<InternalBelaContext> ctxs(2);
	InternalBelaContext& ctx1 = ctxs[0];
	InternalBelaContext& ctx2 = ctxs[1];
	for(auto& ctx : ctxs)
	{
		memset((void*)&ctx, 0, sizeof(ctx));
		ctx.audioFrames = 16;
		ctx.analogFrames = 8;
		ctx.digitalFrames = 16;
		ctx.audioInChannels = 2;
		ctx.audioOutChannels = 2;
		ctx.analogInChannels = 8;
		ctx.analogOutChannels = 8;
		contextAllocate(&ctx);
	}
	assert(ctx1.audioFrames != ctx1.analogFrames);
	for(unsigned int n = 0; n < ctx1.audioFrames * ctx1.audioInChannels; ++n)
		ctx1.audioIn[n] = n;
	for(unsigned int n = 0; n < ctx1.audioFrames * ctx1.audioOutChannels; ++n)
		ctx1.audioOut[n] = n;
	for(unsigned int n = 0; n < ctx1.analogFrames * ctx1.analogInChannels; ++n)
		ctx1.analogIn[n] = n;
	for(unsigned int n = 0; n < ctx1.analogFrames * ctx1.analogOutChannels; ++n)
		ctx1.analogOut[n] = n;
	for(unsigned int n = 0; n < ctx1.digitalFrames; ++n)
		ctx1.digital[n] = n;
	assert(!contextEqual(&ctx1, &ctx2));
	contextCopy(&ctx1, &ctx2);
	assert(contextEqual(&ctx1, &ctx2));
	InternalBelaContext ctx3;
	contextCopy(&ctx1, &ctx3);
	assert(contextEqual(&ctx1, &ctx3));
	return true;
}
