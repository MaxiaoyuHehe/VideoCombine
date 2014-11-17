/* 
 *最简单的基于FFmpeg的转码器
 *Simplest FFmpeg Transcoder
 *
 *雷霄骅 Lei Xiaohua
 *leixiaohua1020@126.com
 *中国传媒大学/数字电视技术
 *Communication University of China / Digital TV Technology
 *http://blog.csdn.net/leixiaohua1020
 *
 *本程序实现了视频格式之间的转换。是一个最简单的视频转码程序。
 *
 */

#include "stdafx.h"
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfiltergraph.h"
#include "libavfilter/avcodec.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libswscale\swscale.h"
};
#include <math.h>


static AVFormatContext **ifmt_ctx;
static AVFormatContext *ofmt_ctx;
int video_num=4;
 AVFrame **frame = NULL;
int expand_times=0;
typedef struct FilteringContext {
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;
} FilteringContext;
static FilteringContext *filter_ctx;
/**
 * 计算各个支路视频的左上角的位置
 * @video_num 支路视频的个数
 * @sub_video_pos 第[i][0]个子元素存储第i支路视频左上角的纵坐标 第[i][1]个子元素存储第i支路视频左上角的横坐标。子视频的排序方法为从左到右、从上到下
 * @height   支路视频的高度
 * @width 支路视频的宽度
 * 此时的输出视频的像素数是原始格式的video_num倍 想保持分辨率不变的话需要用video_filter进行伸缩变换
 */
void cal_sub_video_pos(int video_num,int** sub_video_pos,int height,int width)
{
	
	int Len=(int)sqrt((double)video_num);
	for (int i=0;i<Len;i++)
	{
		for (int j=0;j<Len;j++)
		{
			sub_video_pos[i*Len+j][0]=(i)*height;
			sub_video_pos[i*Len+j][1]=(j)*width;
		}
	}
}
static int open_input_file( char **filename,int video_num)
{
    int ret;
    unsigned int i;
	unsigned int j;
    ifmt_ctx = (AVFormatContext**)av_malloc_array(video_num,sizeof(AVFormatContext*));
    for(i=0;i<video_num;i++)
	{
		*(ifmt_ctx+i)=NULL;
		if ((ret = avformat_open_input((ifmt_ctx+i), filename[i], NULL, NULL)) < 0) {
			av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
			return ret;
		}
		if ((ret = avformat_find_stream_info(ifmt_ctx[i], NULL)) < 0) {
			av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
			return ret;
		}
		for (j = 0; j < ifmt_ctx[i]->nb_streams; j++) {
			AVStream *stream;
			AVCodecContext *codec_ctx;
			stream = ifmt_ctx[i]->streams[j];
			codec_ctx = stream->codec;
			/* Reencode video & audio and remux subtitles etc. */
			if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
                || codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* Open decoder */
				ret = avcodec_open2(codec_ctx,
					avcodec_find_decoder(codec_ctx->codec_id), NULL);
				if (ret < 0) {
					av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
					return ret;
				}
			}
		}
		av_dump_format(ifmt_ctx[i], 0, filename[i], 0);
	}
    return 0;
}
static int open_output_file(const char *filename,int *video_index)
{
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    AVCodec *encoder;
    int ret;
    unsigned int i;
    ofmt_ctx = NULL;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, filename);
    if (!ofmt_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
        return AVERROR_UNKNOWN;
    }
    for (i = 0; i < ifmt_ctx[0]->nb_streams; i++) {
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }
        in_stream = ifmt_ctx[0]->streams[i];
        dec_ctx = in_stream->codec;
        enc_ctx = out_stream->codec;
        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
                || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* in this example, we choose transcoding to same codec */
            encoder = avcodec_find_encoder(dec_ctx->codec_id);
            /* In this example, we transcode to same properties (picture size,
             * sample rate etc.). These properties can be changed for output
             * streams easily using filters */
            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                *video_index=i;
				//放大倍数
				enc_ctx->height = dec_ctx->height;
                enc_ctx->width = dec_ctx->width;
                enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
                /* take first format from list of supported formats */
                enc_ctx->pix_fmt = encoder->pix_fmts[0];
				enc_ctx->me_range=16;
				enc_ctx->max_qdiff=4;
				enc_ctx->qcompress=0.6;
                /* video time_base can be set to whatever is handy and supported by encoder */
                enc_ctx->time_base = dec_ctx->time_base;
            } else {
                enc_ctx->sample_rate = dec_ctx->sample_rate;
                enc_ctx->channel_layout = dec_ctx->channel_layout;
                enc_ctx->channels = av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
                /* take first format from list of supported formats */
                enc_ctx->sample_fmt = encoder->sample_fmts[0];
				AVRational time_base={1, enc_ctx->sample_rate};
                enc_ctx->time_base = time_base;
            }
            /* Third parameter can be used to pass settings to encoder */
            ret = avcodec_open2(enc_ctx, encoder, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n", i);
                return ret;
            }
        } else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            av_log(NULL, AV_LOG_FATAL, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return AVERROR_INVALIDDATA;
        } else {
            /* if this stream must be remuxed */
            ret = avcodec_copy_context(ofmt_ctx->streams[i]->codec,
                    ifmt_ctx[0]->streams[i]->codec);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Copying stream context failed\n");
                return ret;
            }
        }
        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            enc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
    av_dump_format(ofmt_ctx, 0, filename, 1);
    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", filename);
            return ret;
        }
    }
    /* init muxer, write output file header */
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }
    return 0;
}
static int init_filter(FilteringContext* fctx, AVCodecContext *dec_ctx,
        AVCodecContext *enc_ctx, const char *filter_spec)
{
    char args[512];
    int ret = 0;
    AVFilter *buffersrc = NULL;
    AVFilter *buffersink = NULL;
    AVFilterContext *buffersrc_ctx = NULL;
    AVFilterContext *buffersink_ctx = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVFilterGraph *filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        _snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                dec_ctx->width*expand_times, dec_ctx->height*expand_times, dec_ctx->pix_fmt,
                dec_ctx->time_base.num, dec_ctx->time_base.den,
                dec_ctx->sample_aspect_ratio.num,
                dec_ctx->sample_aspect_ratio.den);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }
        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }
        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",
                (uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        if (!dec_ctx->channel_layout)
            dec_ctx->channel_layout =
                av_get_default_channel_layout(dec_ctx->channels);
        _snprintf(args, sizeof(args),
                "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%I64x",
                dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
                av_get_sample_fmt_name(dec_ctx->sample_fmt),
                dec_ctx->channel_layout);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }
        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }
        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
                (uint8_t*)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }
        ret = av_opt_set_bin(buffersink_ctx, "channel_layouts",
                (uint8_t*)&enc_ctx->channel_layout,
                sizeof(enc_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }
        ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
                (uint8_t*)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
            goto end;
        }
    } else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }
    /* Endpoints for the filter graph. */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;
    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_spec,
                    &inputs, &outputs, NULL)) < 0)
        goto end;
    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;
    /* Fill FilteringContext */
    fctx->buffersrc_ctx = buffersrc_ctx;
    fctx->buffersink_ctx = buffersink_ctx;
    fctx->filter_graph = filter_graph;
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return ret;
}
static int init_filters(void)
{
    char filter_spec[256];
    unsigned int i;
    int ret;
    filter_ctx = (FilteringContext *)av_malloc_array(ifmt_ctx[0]->nb_streams, sizeof(*filter_ctx));
    if (!filter_ctx)
        return AVERROR(ENOMEM);
    for (i = 0; i < ifmt_ctx[0]->nb_streams; i++) {
        filter_ctx[i].buffersrc_ctx  = NULL;
        filter_ctx[i].buffersink_ctx = NULL;
        filter_ctx[i].filter_graph   = NULL;
        if (!(ifmt_ctx[0]->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO
                || ifmt_ctx[0]->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO))
            continue;
        if (ifmt_ctx[0]->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        _snprintf(filter_spec, sizeof(filter_spec),"scale=w=iw/%d:h=ih/%d,lutyuv=u=128:v=128",expand_times,expand_times);/* passthrough (dummy) filter for video */
        else
            _snprintf(filter_spec, sizeof(filter_spec),"anull");/* passthrough (dummy) filter for audio */
        ret = init_filter(&filter_ctx[i], ifmt_ctx[0]->streams[i]->codec,
                ofmt_ctx->streams[i]->codec, filter_spec);
        if (ret)
            return ret;
    }
    return 0;
}
static int encode_write_frame(AVFrame *filt_frame, unsigned int stream_index, int *got_frame) {
    int ret;
    int got_frame_local;
    AVPacket enc_pkt;
    int (*enc_func)(AVCodecContext *, AVPacket *, const AVFrame *, int *) =
        (ofmt_ctx->streams[stream_index]->codec->codec_type ==
         AVMEDIA_TYPE_VIDEO) ? avcodec_encode_video2 : avcodec_encode_audio2;
    if (!got_frame)
        got_frame = &got_frame_local;
    av_log(NULL, AV_LOG_INFO, "Encoding frame\n");
    /* encode filtered frame */
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    av_init_packet(&enc_pkt);
    ret = enc_func(ofmt_ctx->streams[stream_index]->codec, &enc_pkt,
            filt_frame, got_frame);
    av_frame_free(&filt_frame);
    if (ret < 0)
        return ret;
    if (!(*got_frame))
        return 0;
    /* prepare packet for muxing */
    enc_pkt.stream_index = stream_index;
    enc_pkt.dts = av_rescale_q_rnd(enc_pkt.dts,
            ofmt_ctx->streams[stream_index]->codec->time_base,
            ofmt_ctx->streams[stream_index]->time_base,
            (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
    enc_pkt.pts = av_rescale_q_rnd(enc_pkt.pts,
            ofmt_ctx->streams[stream_index]->codec->time_base,
            ofmt_ctx->streams[stream_index]->time_base,
            (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
    enc_pkt.duration = av_rescale_q(enc_pkt.duration,
            ofmt_ctx->streams[stream_index]->codec->time_base,
            ofmt_ctx->streams[stream_index]->time_base);
    av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
    /* mux encoded frame */
    ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
    return ret;
}
static int filter_encode_write_frame(AVFrame *frame, unsigned int stream_index)
{
    int ret;
    AVFrame *filt_frame;
    av_log(NULL, AV_LOG_INFO, "Pushing decoded frame to filters\n");
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(filter_ctx[stream_index].buffersrc_ctx,
            frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return ret;
    }
    /* pull filtered frames from the filtergraph */
    while (1) {
        filt_frame = av_frame_alloc();
        if (!filt_frame) {
            ret = AVERROR(ENOMEM);
            break;
        }
        av_log(NULL, AV_LOG_INFO, "Pulling filtered frame from filters\n");
        ret = av_buffersink_get_frame(filter_ctx[stream_index].buffersink_ctx,
                filt_frame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns AVERROR_EOF
             * rewrite retcode to 0 to show it as normal procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            av_frame_free(&filt_frame);
            break;
        }
        filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write_frame(filt_frame, stream_index, NULL);
        if (ret < 0)
            break;
    }
    return ret;
}
static int flush_encoder(unsigned int stream_index)
{
    int ret;
    int got_frame;
    if (!(ofmt_ctx->streams[stream_index]->codec->codec->capabilities &
                CODEC_CAP_DELAY))
        return 0;
    while (1) {
        av_log(NULL, AV_LOG_INFO, "Flushing stream #%u encoder\n", stream_index);
        ret = encode_write_frame(NULL, stream_index,&got_frame);
        if (ret < 0)
            break;
        if (!got_frame)
            return 0;
    }
    return ret;
}

int _tmain(int argc, _TCHAR* argv[])
{
	int ret;
    AVPacket packet;
  
	expand_times=sqrt((double)video_num);
    enum AVMediaType type;
    unsigned int stream_index;
    unsigned int i,j,k,l;
    int got_frame;
	int size;
	uint8_t* picture_buf=NULL;
	int ** sub_video_pos=NULL;
	int should_break=0;
	int (*dec_func)(AVCodecContext *, AVFrame *, int *, const AVPacket *);
	int * isEnd=NULL;
	//这里的height和width是支路视频的宽和高 
	int height,width;
	AVCodecContext* out_video_codec=NULL;
	int video_index=-1;
	AVFrame * out_frame=NULL;
	frame = (AVFrame**)av_malloc_array(video_num,sizeof(AVFrame*));
	uint8_t **out_buffer=(uint8_t**)malloc(video_num*sizeof(uint8_t*));
	char **filenames=(char**)malloc(video_num*sizeof(char*));
	for(i=0;i<video_num;i++)
	{
		filenames[i]=(char*)malloc(10*sizeof(char));
	}
	sub_video_pos=(int**)malloc(video_num*sizeof(int *));
	for(i=0;i<video_num;i++)
	{
		sub_video_pos[i]=(int*)malloc(2*sizeof(int));
	}
	
	isEnd = (int *)malloc(sizeof(int)*video_num);
    if (argc != 3) {
        av_log(NULL, AV_LOG_ERROR, "Usage: %s <input file> <output file>\n", argv[0]);
        return 1;
    }
    av_register_all();
    avfilter_register_all();
	strcpy(filenames[0],"nwn.mp4");
	strcpy(filenames[1],"nwn.mp4");
	strcpy(filenames[2],"nwn.mp4");
	strcpy(filenames[3],"nwn.mp4");
	//strcpy(filenames[4],"nwn.mp4");
	//strcpy(filenames[5],"nwn.mp4");
	//strcpy(filenames[6],"nwn.mp4");
	//strcpy(filenames[7],"nwn.mp4");
	//strcpy(filenames[8],"nwn.mp4");
	if ((ret = open_input_file(filenames,video_num)) < 0)
        goto end;
    if ((ret = open_output_file(argv[2],&video_index)) < 0)
        goto end;
    if ((ret = init_filters()) < 0)
        goto end;
	out_video_codec=ofmt_ctx->streams[video_index]->codec;
	//将存储输出YUV数据的变量out_frame的height width linesize等参数确定下来
	out_frame=av_frame_alloc();
	size = avpicture_get_size(out_video_codec->pix_fmt, out_video_codec->width*expand_times, out_video_codec->height*expand_times);
	picture_buf = (uint8_t *)av_malloc(size);
	avpicture_fill((AVPicture *)out_frame, picture_buf, out_video_codec->pix_fmt, out_video_codec->width*expand_times, out_video_codec->height*expand_times);
	int y_size=0;
	cal_sub_video_pos(video_num,sub_video_pos,out_video_codec->height,out_video_codec->width);	
	height=	out_video_codec->height;
	width=out_video_codec->width;
    /* read all packets */
    while (1) {
		for(i=0;i<video_num;i++)
		{
			isEnd[i]=av_read_frame(ifmt_ctx[i],&packet);
			while(isEnd[i]>=0)
			{
				stream_index = packet.stream_index;
				type=ifmt_ctx[i]->streams[stream_index]->codec->codec_type;
				if(filter_ctx[stream_index].filter_graph)
				{
					if(type==AVMEDIA_TYPE_VIDEO)
					{
						frame[i]=av_frame_alloc();
						if(!(frame[i]))
						{
							ret=AVERROR(ENOMEM);
							break;
						}
						packet.dts = av_rescale_q_rnd(packet.dts,
							ifmt_ctx[0]->streams[stream_index]->time_base,
							ifmt_ctx[0]->streams[stream_index]->codec->time_base,
							(AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
						packet.pts = av_rescale_q_rnd(packet.pts,
							ifmt_ctx[0]->streams[stream_index]->time_base,
							ifmt_ctx[0]->streams[stream_index]->codec->time_base,
							(AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
						ret=avcodec_decode_video2(ifmt_ctx[i]->streams[stream_index]->codec, frame[i],&got_frame, &packet);
						if (ret < 0) 
						{
							av_frame_free(&frame[i]);
							av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
							break;
						}
						if (got_frame) {
							frame[i]->pts = av_frame_get_best_effort_timestamp(frame[i]);
							printf("Get Video Num\n %d%d%d%d%d%d\n",i,i,i,i,i,i);
							break;
						} else 
						{
							av_frame_free(&(frame[i]));
							int a=0;
						}
					}
				}
				else {
					/* remux this frame without reencoding */
					packet.dts = av_rescale_q_rnd(packet.dts,
						ifmt_ctx[0]->streams[stream_index]->time_base,
						ofmt_ctx->streams[stream_index]->time_base,
						(AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
					packet.pts = av_rescale_q_rnd(packet.pts,
						ifmt_ctx[0]->streams[stream_index]->time_base,
						ofmt_ctx->streams[stream_index]->time_base,
						(AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
				}
				av_free_packet(&packet);
				isEnd[i]=av_read_frame(ifmt_ctx[i],&packet);
			}
		}
		for(i=0;i<video_num;i++)
		{
			if(isEnd[i]<0)
				should_break=1;
		}
		if(should_break==1)
			break;
		else
		{
			//进行各帧合并 并进行编码
			for(i=0;i<video_num;i++)
			{
				for(j=0;j<height;j++)
				{
					memcpy(&(out_frame->data[0][(j+sub_video_pos[i][0])*expand_times*width+sub_video_pos[i][1]]),&(frame[i]->data[0][j*frame[i]->linesize[0]]),width);
					//for(k=0;k<width;k++)
					//{
					//	out_frame->data[0][(j+sub_video_pos[i][0])*expand_times*width+k+sub_video_pos[i][1]]=frame[i]->data[0][j*frame[i]->linesize[0]+k];
					//}
				}
				for(j=0;j<height/2;j++)
				{
					memcpy(&(out_frame->data[1][(j+sub_video_pos[i][0]/2)*expand_times*width/2+sub_video_pos[i][1]/2]),&(frame[i]->data[1][j*frame[i]->linesize[1]]),width/2);
					memcpy(&(out_frame->data[2][(j+sub_video_pos[i][0]/2)*expand_times*width/2+sub_video_pos[i][1]/2]),&(frame[i]->data[2][j*frame[i]->linesize[2]]),width/2);
					//for(k=0;k<width/2;k++)
					//{
					//	out_frame->data[1][(j+sub_video_pos[i][0]/2)*expand_times*width/2+k+sub_video_pos[i][1]/2]=frame[i]->data[1][j*frame[i]->linesize[1]+k];
					//	out_frame->data[2][(j+sub_video_pos[i][0]/2)*expand_times*width/2+k+sub_video_pos[i][1]/2]=frame[i]->data[2][j*frame[i]->linesize[2]+k];
					//}
				}
			}
			out_frame->width=width*expand_times;
			out_frame->height=height*expand_times;
			out_frame->pts=frame[0]->pts;
			out_frame->format=frame[0]->format;
			ret = filter_encode_write_frame(out_frame, 0);
		}

	}
 
	
    /* flush filters and encoders */
    for (i = 0; i < ofmt_ctx->nb_streams; i++) {
        /* flush filter */
        if (!filter_ctx[i].filter_graph)
            continue;
         ret = filter_encode_write_frame(NULL, i);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing filter failed\n");
            goto end;
        }
        /* flush encoder */
        ret = flush_encoder(i);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
            goto end;
        }
    }
    av_write_trailer(ofmt_ctx);
end:
    av_free_packet(&packet);
    for(i=0;i<video_num;i++)
		av_frame_free(&(frame[i]));
    for (i = 0; i < ifmt_ctx[0]->nb_streams; i++) {
        avcodec_close(ifmt_ctx[0]->streams[i]->codec);
        if (ofmt_ctx && ofmt_ctx->nb_streams > i && ofmt_ctx->streams[i] && ofmt_ctx->streams[i]->codec)
            avcodec_close(ofmt_ctx->streams[i]->codec);
        if (filter_ctx && filter_ctx[i].filter_graph)
            avfilter_graph_free(&filter_ctx[i].filter_graph);
    }
	
    av_free(filter_ctx);
    avformat_close_input(&(ifmt_ctx[0]));
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "Error occurred\n");
    return (ret? 1:0);
}

