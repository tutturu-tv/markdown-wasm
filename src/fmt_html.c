/*
 * md4c modified for mdjs.
 * Original source code is licensed as follows:
 *
 * Copyright (c) 2016-2019 Martin Mitas
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "common.h"
#include "fmt_html.h"
#include "md4c.h"

typedef struct HtmlRenderer_st {
  WBuf* outbuf;
  int   imgnest;
  int   addanchor;
} HtmlRenderer;


static char htmlEscapeMap[256] = {
        /* 0 1 2 3 4 5 6 7 8 9 A B C D E F */
/* 0x00 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // <CTRL> ...
/* 0x10 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // <CTRL> ...
/* 0x20 */ 0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,  //   ! " # $ % & ' ( ) * + , - . /
/* 0x30 */ 0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,0,  // 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
/* 0x40 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // @ A B C D E F G H I J K L M N O
/* 0x50 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // P Q R S T U V W X Y Z [ \ ] ^ _
/* 0x60 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // ` a b c d e f g h i j k l m n o
/* 0x70 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // p q r s t u v w x y z { | } ~ <DEL>
/* 0x80 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // <CTRL> ...
/* 0x90 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // <CTRL> ...
/* 0xA0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // <NBSP> ¡ ¢ £ ¤ ¥ ¦ § ¨ © ª « ¬ <SOFTHYPEN> ® ¯
/* 0xB0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // ° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½ ¾ ¿
/* 0xC0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // À Á Â Ã Ä Å Æ Ç È É Ê Ë Ì Í Î Ï
/* 0xD0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // Ð Ñ Ò Ó Ô Õ Ö × Ø Ù Ú Û Ü Ý Þ ß
/* 0xE0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // à á â ã ä å æ ç è é ê ë ì í î ï
/* 0xF0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // ð ñ ò ó ô õ ö ÷ ø ù ú û ü ý þ ÿ
};

static const char ucReplacementUTF8[] = { 0xef, 0xbf, 0xbd };


static inline void render_text(HtmlRenderer* r, const char* pch, size_t len) {
  WBufAppendBytes(r->outbuf, pch, len);
}

static inline void render_literal(HtmlRenderer* r, const char* cs) {
  WBufAppendBytes(r->outbuf, cs, strlen(cs));
}

static inline void render_char(HtmlRenderer* r, char c) {
  WBufAppendc(r->outbuf, c);
}


static void render_html_escaped(HtmlRenderer* r, const char* data, size_t size) {
  MD_OFFSET beg = 0;
  MD_OFFSET off = 0;

  /* Some characters need to be escaped in normal HTML text. */
  #define HTML_NEED_ESCAPE(ch)  (htmlEscapeMap[(unsigned char)(ch)] != 0)

  while (1) {
    while (
      off + 3 < size &&
      !HTML_NEED_ESCAPE(data[off+0]) &&
      !HTML_NEED_ESCAPE(data[off+1]) &&
      !HTML_NEED_ESCAPE(data[off+2]) &&
      !HTML_NEED_ESCAPE(data[off+3])
    ) {
      off += 4;
    }

    while (off < size  &&  !HTML_NEED_ESCAPE(data[off])) {
      off++;
    }

    if (off > beg) {
      render_text(r, data + beg, off - beg);
    }

    if (off < size) {
      switch (data[off]) {
        case '&': render_literal(r, "&amp;");  break;
        case '<': render_literal(r, "&lt;");   break;
        case '>': render_literal(r, "&gt;");   break;
        case '"': render_literal(r, "&quot;"); break;
      }
      off++;
    } else {
      break;
    }
    beg = off;
  }
}


static char slugMap[256] = {
/*          0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F    */
/* 0x00 */ '-','-','-','-','-','-','-','-','-','-','-','-','-','-','-','-',  // <CTRL> ...
/* 0x10 */ '-','-','-','-','-','-','-','-','-','-','-','-','-','-','-','-',  // <CTRL> ...
/* 0x20 */ '-','-','-','-','-','-','-','-','-','-','-','-','-','-','.','-',  //   ! " # $ % & ' ( ) * + , - . /
/* 0x30 */ '0','1','2','3','4','5','6','7','8','9','-','-','-','-','-','-',  // 0 1 2 3 4 5 6 7 8 9 : ; < = > ?
/* 0x40 */ '-','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',  // @ A B C D E F G H I J K L M N O
/* 0x50 */ 'p','q','r','s','t','u','v','w','x','y','z','-','-','-','-','_',  // P Q R S T U V W X Y Z [ \ ] ^ _
/* 0x60 */ '-','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',  // ` a b c d e f g h i j k l m n o
/* 0x70 */ 'p','q','r','s','t','u','v','w','x','y','z','-','-','-','-','-',  // p q r s t u v w x y z { | } ~ <DEL>
/* 0x80 */ '-','-','-','-','-','-','-','-','-','-','-','-','-','-','-','-',  // <CTRL> ...
/* 0x90 */ '-','-','-','-','-','-','-','-','-','-','-','-','-','-','-','-',  // <CTRL> ...
/* 0xA0 */ '-','-','-','-','-','-','-','-','-','-','-','-','-','-','-','-',  // <NBSP> ¡ ¢ £ ¤ ¥ ¦ § ¨ © ª « ¬ <SOFTHYPEN> ® ¯
/* 0xB0 */ '-','-','-','-','-','-','-','-','-','-','-','-','-','-','-','-',  // ° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½ ¾ ¿
/* 0xC0 */ 'a','a','a','a','a','a','a','c','e','e','e','e','i','i','i','i',  // À Á Â Ã Ä Å Æ Ç È É Ê Ë Ì Í Î Ï
/* 0xD0 */ 'd','n','o','o','o','o','o','x','o','u','u','u','u','y','-','s',  // Ð Ñ Ò Ó Ô Õ Ö × Ø Ù Ú Û Ü Ý Þ ß
/* 0xE0 */ 'a','a','a','a','a','a','a','c','e','e','e','e','i','i','i','i',  // à á â ã ä å æ ç è é ê ë ì í î ï
/* 0xF0 */ 'd','n','o','o','o','o','o','-','o','u','u','u','u','y','-','y',  // ð ñ ò ó ô õ ö ÷ ø ù ú û ü ý þ ÿ
};


static size_t WBufAppendSlug(WBuf* b, const char* pch, size_t len) {
  WBufReserve(b, len);
  const char* start = b->ptr;
  char c = 0, pc = 0;
  for (size_t i = 0; i < len; i++) {
    u8 x = (u8)pch[i];
    if (x >= 0x80) {
      // decode UTF8-encoded character as Latin-1
      if ((x >> 5) == 0x6 && i+1 < len) {
        u32 cp = ((x << 6) & 0x7ff) + ((pch[++i]) & 0x3f);
        x = cp <= 0xFF ? cp : 0;
      } else {
        x = 0;
      }
    }
    c = slugMap[x];
    if (c != '-' || pc != '-' && pc) {
      // note: check "pc" to trim leading '-'
      *(b->ptr++) = c;
      pc = c;
    }
  }
  if (pc == '-') {
    // trim trailing '-'
    b->ptr--;
  }
  return b->ptr - start;
}


static void render_attribute(HtmlRenderer* r, const MD_ATTRIBUTE* attr) {
  int i;
  for (i = 0; attr->substr_offsets[i] < attr->size; i++) {
    MD_TEXTTYPE type = attr->substr_types[i];
    MD_OFFSET   off  = attr->substr_offsets[i];
    MD_SIZE     size = attr->substr_offsets[i+1] - off;
    const MD_CHAR* text = attr->text + off;
    switch (type) {
      case MD_TEXT_NULLCHAR: render_text(r, ucReplacementUTF8, sizeof(ucReplacementUTF8)); break;
      case MD_TEXT_ENTITY:   render_text(r, text, size); break;
      default:               render_html_escaped(r, text, size); break;
    }
  }
}


// static void render_open_ol_block(HtmlRenderer* r, const MD_BLOCK_OL_DETAIL* det) {
//   if (det->start == 1) {
//     render_literal(r, "<ol>\n");
//   } else {
//     render_literal(r, "<ol start=\"");
//     WBufAppendU32(r->outbuf, det->start, 10);
//     render_literal(r, "\">\n");
//   }
// }

// static void render_open_li_block(HtmlRenderer* r, const MD_BLOCK_LI_DETAIL* det) {
//   if (det->is_task) {
//     render_literal(r, "<li class=\"task-list-item\"><input type=\"checkbox\" disabled");
//     if (det->task_mark == 'x' || det->task_mark == 'X') {
//       render_literal(r, " checked");
//     }
//     render_char(r, '>');
//   } else {
//     render_literal(r, "<li>");
//   }
// }

static void render_open_code_block(HtmlRenderer* r, const MD_BLOCK_CODE_DETAIL* det) {
  render_literal(r, "<pre><code");
  if (det->lang.text != NULL) {
    render_literal(r, " class=\"language-");
    render_attribute(r, &det->lang);
    render_char(r, '"');
  }
  render_char(r, '>');
}

static void render_open_td_block(HtmlRenderer* r, bool isTH, const MD_BLOCK_TD_DETAIL* det) {
  render_text(r, isTH ? "<th" : "<td", 3);
  switch (det->align) {
    case MD_ALIGN_LEFT:   render_literal(r, " align=\"left\">"); break;
    case MD_ALIGN_CENTER: render_literal(r, " align=\"center\">"); break;
    case MD_ALIGN_RIGHT:  render_literal(r, " align=\"right\">"); break;
    default:              render_char(r, '>'); break;
  }
}

static void render_open_a_span(HtmlRenderer* r, const MD_SPAN_A_DETAIL* det) {
  render_literal(r, "<a href=\"");
  render_attribute(r, &det->href);
  if (det->title.text != NULL) {
    render_literal(r, "\" title=\"");
    render_attribute(r, &det->title);
  }
  render_literal(r, "\">");
}

static void render_open_img_span(HtmlRenderer* r, const MD_SPAN_IMG_DETAIL* det) {
  render_literal(r, "<img src=\"");
  render_attribute(r, &det->src);
  render_literal(r, "\" alt=\"");
  r->imgnest++;
}

static void render_close_img_span(HtmlRenderer* r, const MD_SPAN_IMG_DETAIL* det) {
  if(det->title.text != NULL) {
    render_literal(r, "\" title=\"");
    render_attribute(r, &det->title);
  }
  render_literal(r, "\">");
  r->imgnest--;
}

static void render_open_wikilink_span(HtmlRenderer* r, const MD_SPAN_WIKILINK_DETAIL* det) {
  render_literal(r, "<x-wikilink data-target=\"");
  render_attribute(r, &det->target);
  render_literal(r, "\">");
}


/**************************************
 ***  HTML renderer implementation  ***
 **************************************/



static int enter_block_callback(MD_BLOCKTYPE type, void* detail, void* userdata) {
  // static const MD_CHAR* head[6] = { "<h1>", "<h2>", "<h3>", "<h4>", "<h5>", "<h6>" };
  HtmlRenderer* r = (HtmlRenderer*) userdata;

  switch(type) {
    case MD_BLOCK_DOC:      /* noop */ break;
    case MD_BLOCK_QUOTE:    render_literal(r, "<blockquote>"); break;
    // case MD_BLOCK_UL:       render_literal(r, "<ul>\n"); break;
    // case MD_BLOCK_OL:       render_open_ol_block(r, (const MD_BLOCK_OL_DETAIL*)detail); break;
    // case MD_BLOCK_LI:       render_open_li_block(r, (const MD_BLOCK_LI_DETAIL*)detail); break;
    // case MD_BLOCK_HR:       render_literal(r, "<hr>\n"); break;
    // case MD_BLOCK_H:
    // {
    //   render_literal(r, head[((MD_BLOCK_H_DETAIL*)detail)->level - 1]);
    //   r->addanchor = 1;
    //   break;
    // }
    case MD_BLOCK_CODE:     render_open_code_block(r, (const MD_BLOCK_CODE_DETAIL*) detail); break;
    // case MD_BLOCK_HTML:     /* noop */ break;
    case MD_BLOCK_P:        /*render_literal(r, "<p>");*/ break;
    // case MD_BLOCK_TABLE:    render_literal(r, "<table>\n"); break;
    // case MD_BLOCK_THEAD:    render_literal(r, "<thead>\n"); break;
    // case MD_BLOCK_TBODY:    render_literal(r, "<tbody>\n"); break;
    // case MD_BLOCK_TR:       render_literal(r, "<tr>\n"); break;
    // case MD_BLOCK_TH:       render_open_td_block(r, true, (MD_BLOCK_TD_DETAIL*)detail); break;
    // case MD_BLOCK_TD:       render_open_td_block(r, false, (MD_BLOCK_TD_DETAIL*)detail); break;
  }

  return 0;
}

static int leave_block_callback(MD_BLOCKTYPE type, void* detail, void* userdata) {
  // static const MD_CHAR* head[6] = { "</h1>\n", "</h2>\n", "</h3>\n", "</h4>\n", "</h5>\n", "</h6>\n" };
  HtmlRenderer* r = (HtmlRenderer*) userdata;

  switch(type) {
    case MD_BLOCK_DOC:      /*noop*/ break;
    case MD_BLOCK_QUOTE:    render_literal(r, "</blockquote>"); break;
    // case MD_BLOCK_UL:       render_literal(r, "</ul>\n"); break;
    // case MD_BLOCK_OL:       render_literal(r, "</ol>\n"); break;
    // case MD_BLOCK_LI:       render_literal(r, "</li>\n"); break;
    // case MD_BLOCK_HR:       /*noop*/ break;
    // case MD_BLOCK_H:        render_literal(r, head[((MD_BLOCK_H_DETAIL*)detail)->level - 1]); break;
    case MD_BLOCK_CODE:     render_literal(r, "</code></pre>"); break;
    // case MD_BLOCK_HTML:     /* noop */ break;
    case MD_BLOCK_P:        /*render_literal(r, "</p>\n");*/ break;
    // case MD_BLOCK_TABLE:    render_literal(r, "</table>\n"); break;
    // case MD_BLOCK_THEAD:    render_literal(r, "</thead>\n"); break;
    // case MD_BLOCK_TBODY:    render_literal(r, "</tbody>\n"); break;
    // case MD_BLOCK_TR:       render_literal(r, "</tr>\n"); break;
    // case MD_BLOCK_TH:       render_literal(r, "</th>\n"); break;
    // case MD_BLOCK_TD:       render_literal(r, "</td>\n"); break;
  }

  return 0;
}

static int enter_span_callback(MD_SPANTYPE type, void* detail, void* userdata) {
  HtmlRenderer* r = (HtmlRenderer*) userdata;

  if(r->imgnest > 0) {
    /* We are inside an image, i.e. rendering the ALT attribute of
     * <IMG> tag. */
    return 0;
  }

  switch(type) {
    case MD_SPAN_EM:                render_literal(r, "<em>"); break;
    case MD_SPAN_STRONG:            render_literal(r, "<b>"); break;
    case MD_SPAN_A:                 render_open_a_span(r, (MD_SPAN_A_DETAIL*) detail); break;
    case MD_SPAN_IMG:               render_open_img_span(r, (MD_SPAN_IMG_DETAIL*) detail); break;
    case MD_SPAN_CODE:              render_literal(r, "<code>"); break;
    case MD_SPAN_DEL:               render_literal(r, "<del>"); break;
    case MD_SPAN_LATEXMATH:         render_literal(r, "<x-equation>"); break;
    case MD_SPAN_LATEXMATH_DISPLAY: render_literal(r, "<x-equation type=\"display\">"); break;
    case MD_SPAN_WIKILINK:          render_open_wikilink_span(r, (MD_SPAN_WIKILINK_DETAIL*) detail); break;
    case MD_SPAN_U:                 render_literal(r, "<u>"); break;
    case MD_SPAN_SPOILER:           render_literal(r, "<span class=\"md-spoiler\">"); break;
  }

  return 0;
}

static int leave_span_callback(MD_SPANTYPE type, void* detail, void* userdata) {
  HtmlRenderer* r = (HtmlRenderer*) userdata;

  if(r->imgnest > 0) {
    /* We are inside an image, i.e. rendering the ALT attribute of
     * <IMG> tag. */
    if(r->imgnest == 1  &&  type == MD_SPAN_IMG)
      render_close_img_span(r, (MD_SPAN_IMG_DETAIL*) detail);
    return 0;
  }

  switch(type) {
    case MD_SPAN_EM:                render_literal(r, "</em>"); break;
    case MD_SPAN_STRONG:            render_literal(r, "</b>"); break;
    case MD_SPAN_A:                 render_literal(r, "</a>"); break;
    case MD_SPAN_IMG:               /*noop, handled above*/ break;
    case MD_SPAN_CODE:              render_literal(r, "</code>"); break;
    case MD_SPAN_DEL:               render_literal(r, "</del>"); break;
    case MD_SPAN_LATEXMATH:         /*fall through*/
    case MD_SPAN_LATEXMATH_DISPLAY: render_literal(r, "</x-equation>"); break;
    case MD_SPAN_WIKILINK:          render_literal(r, "</x-wikilink>"); break;
    case MD_SPAN_U:                 render_literal(r, "</u>"); break;
    case MD_SPAN_SPOILER:           render_literal(r, "</span>"); break;
  }

  return 0;
}

static int text_callback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
  HtmlRenderer* r = (HtmlRenderer*) userdata;

  if (r->addanchor) {
    r->addanchor = 0;
    if (type != MD_TEXT_NULLCHAR && type != MD_TEXT_BR && type != MD_TEXT_SOFTBR) {
      render_literal(r, "<a id=\"");

      const char* slugptr = r->outbuf->ptr;
      size_t sluglen = WBufAppendSlug(r->outbuf, text, size);

      render_literal(r, "\" class=\"anchor\" aria-hidden=\"true\" href=\"#");

      if (sluglen > 0) {
        WBufReserve(r->outbuf, sluglen);
        memcpy(r->outbuf->ptr, slugptr, sluglen);
        r->outbuf->ptr += sluglen;
      }

      render_literal(r, "\"></a>");
    }
  }

  switch(type) {
    case MD_TEXT_NULLCHAR:  render_text(r, ucReplacementUTF8, sizeof(ucReplacementUTF8)); break;
    case MD_TEXT_BR:        render_literal(r, (r->imgnest == 0 ? "<br>\n" : " ")); break;
    case MD_TEXT_SOFTBR:    render_literal(r, (r->imgnest == 0 ? "\n" : " ")); break;
    case MD_TEXT_HTML:      render_text(r, text, size); break;
    case MD_TEXT_ENTITY:    render_text(r, text, size); break;
    default:                render_html_escaped(r, text, size); break;
  }

  return 0;
}

// static void debug_log_callback(const char* msg, void* userdata) {
//   dlog("MD4C: %s\n", msg);
// }

int
fmt_html(const MD_CHAR* input, MD_SIZE input_size, WBuf* outbuf, unsigned parser_flags) {
  HtmlRenderer render = { outbuf, 0, 0 };

  MD_PARSER parser = {
    0,
    parser_flags,
    enter_block_callback,
    leave_block_callback,
    enter_span_callback,
    leave_span_callback,
    text_callback,
    NULL, // debug_log_callback,
    NULL
  };

  return md_parse(input, input_size, &parser, (void*) &render);
}
