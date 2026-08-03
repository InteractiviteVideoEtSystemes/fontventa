#include "medkit/log.h"
#include "medkit/videorescaler.h"
extern "C"
{
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/rational.h>
}
#include <stdio.h>

VideoRescaler::VideoRescaler()
{
	graph   = NULL;
	srcCtx  = NULL;
	sinkCtx = NULL;
	curInW  = curInH = curInFmt = 0;
	curOutW = curOutH = 0;
	curHwFramesCtx = NULL;
}

VideoRescaler::~VideoRescaler()
{
	Release();
}

void VideoRescaler::Release()
{
	if (graph)
		avfilter_graph_free(&graph);   // libère aussi srcCtx/sinkCtx
	graph   = NULL;
	srcCtx  = NULL;
	sinkCtx = NULL;
	curInW  = curInH = curInFmt = 0;
	curOutW = curOutH = 0;
	curHwFramesCtx = NULL;
}

bool VideoRescaler::Configure(int inW, int inH, int inFmt, int outW, int outH, AVBufferRef* hwFramesCtx)
{
	// Réutilise le graphe existant si rien n'a changé.
	if (graph && inW==curInW && inH==curInH && inFmt==curInFmt &&
	    outW==curOutW && outH==curOutH && hwFramesCtx==curHwFramesCtx)
		return true;

	Release();

	graph = avfilter_graph_alloc();
	if (!graph)
		return false;

	const bool gpu = (inFmt == AV_PIX_FMT_VAAPI);

	// Source (buffer).
	char args[512];
	snprintf(args, sizeof(args),
	         "video_size=%dx%d:pix_fmt=%d:time_base=1/1:pixel_aspect=1/1",
	         inW, inH, inFmt);

	int ret = avfilter_graph_create_filter(&srcCtx, avfilter_get_by_name("buffer"), "in", args, NULL, graph);
	if (ret < 0) { Release(); return false; }

	// Pour les filtres *_vaapi, le buffersrc doit connaître le hw_frames_ctx.
	if (gpu && hwFramesCtx)
	{
		AVBufferSrcParameters* par = av_buffersrc_parameters_alloc();
		if (!par) { Release(); return false; }
		par->format        = inFmt;
		par->width         = inW;
		par->height        = inH;
		par->time_base     = av_make_q(1, 1);
		par->hw_frames_ctx = hwFramesCtx;
		ret = av_buffersrc_parameters_set(srcCtx, par);
		av_free(par);
		if (ret < 0) { Release(); return false; }
	}

	// Puits (buffersink).
	ret = avfilter_graph_create_filter(&sinkCtx, avfilter_get_by_name("buffersink"), "out", NULL, NULL, graph);
	if (ret < 0) { Release(); return false; }

	// Filtre de mise à l'échelle : matériel (VAAPI) ou logiciel (+format yuv420p
	// pour garantir la sortie attendue par les encodeurs).
	char desc[128];
	if (gpu)
		snprintf(desc, sizeof(desc), "scale_vaapi=w=%d:h=%d", outW, outH);
	else
		snprintf(desc, sizeof(desc), "scale=%d:%d,format=yuv420p", outW, outH);

	AVFilterInOut* outputs = avfilter_inout_alloc();
	AVFilterInOut* inputs  = avfilter_inout_alloc();
	bool ok = false;
	if (outputs && inputs)
	{
		outputs->name = av_strdup("in");  outputs->filter_ctx = srcCtx;  outputs->pad_idx = 0; outputs->next = NULL;
		inputs->name  = av_strdup("out"); inputs->filter_ctx  = sinkCtx; inputs->pad_idx  = 0; inputs->next  = NULL;
		ret = avfilter_graph_parse_ptr(graph, desc, &inputs, &outputs, NULL);
		if (ret >= 0)
			ret = avfilter_graph_config(graph, NULL);
		ok = (ret >= 0);
	}
	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);

	if (!ok)
	{
		Error("-VideoRescaler: graph config failed (%d) [%s]\n", ret, desc);
		Release();
		return false;
	}

	curInW = inW; curInH = inH; curInFmt = inFmt;
	curOutW = outW; curOutH = outH; curHwFramesCtx = hwFramesCtx;
	return true;
}

PictPtr VideoRescaler::Rescale(const PictPtr& in, int width, int height, bool keepRatio)
{
	if (!in || !in->GetAVFrame() || width <= 0)
		return nullptr;

	AVFrame* src = in->GetAVFrame();

	int outW = width;
	int outH = height;
	if (keepRatio)
	{
		if (src->width <= 0)
			return nullptr;
		// Conserve la largeur, recalcule la hauteur effective d'après le ratio.
		outH = (int)((int64_t) width * src->height / src->width);
		outH &= ~1;               // pair (YUV420)
		if (outH <= 0) outH = 2;
	}
	else if (height <= 0)
	{
		return nullptr;
	}

	// Déjà à la taille cible : partage zéro-copie.
	if (src->width == outW && src->height == outH)
		return in;

	if (!Configure(src->width, src->height, src->format, outW, outH, src->hw_frames_ctx))
		return nullptr;

	// Pousse une référence de la trame (KEEP_REF : le graphe fait sa propre ref).
	int ret = av_buffersrc_add_frame_flags(srcCtx, src, AV_BUFFERSRC_FLAG_KEEP_REF);
	if (ret < 0)
	{
		Error("-VideoRescaler: buffersrc_add_frame failed (%d)\n", ret);
		return nullptr;
	}

	AVFrame* out = av_frame_alloc();
	if (!out)
		return nullptr;
	ret = av_buffersink_get_frame(sinkCtx, out);
	if (ret < 0)
	{
		if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
			Error("-VideoRescaler: buffersink_get_frame failed (%d)\n", ret);
		av_frame_free(&out);
		return nullptr;
	}

	return std::make_shared<Pict>(out);
}
