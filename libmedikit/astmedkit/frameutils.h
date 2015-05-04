#if ASTERISK_VERSION_NUM > 10000   // 10600
#define AST_FRAME_GET_BUFFER(fr)        ((unsigned char*)((fr)->data.ptr))
#else
#define AST_FRAME_GET_BUFFER(fr)        ((unsigned char*)((fr)->data))
#endif

#define PKT_PAYLOAD     1450
#define PKT_SIZE        (sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET + PKT_PAYLOAD)
#define PKT_OFFSET      (sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET)

int AstFormatToCodecList(int format, AudioCodec::Type codecList[], unsigned int maxSize);
int AstFormatToCodecList(int format, VideoCodec::Type codecList[], unsigned int maxSize);

inline int AstFormatToCodecList(int format, AudioCodec::Type & ac)
{
	return AstFormatToCodecList(format, &ac, 1);
}

int AstFormatToCodecList(int format, VideoCodec::Type & vc)
{
	return AstFormatToCodecList(format, &vc, 1);
}

bool MediaFrameToAstFrame(const MediaFrame * mf, ast_frame & astf);

int CodecToAstFormat( AudioCodec::Type ac, int & fmt );
int CodecToAstFormat( VideoCodec::Type vc, int & fmt );

