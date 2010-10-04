
#include <string.h>
#include <jni.h>

#include "android/log.h"

#include "pdfview2.h"


#define PDFVIEW_LOG_TAG "cx.hell.android.pdfview"
#define PDFVIEW_MAX_PAGES_LOADED 16



JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *jvm, void *reserved) {
    __android_log_print(ANDROID_LOG_INFO, PDFVIEW_LOG_TAG, "JNI_OnLoad");
    fz_cpudetect();
    fz_accelerate();
    /* pdf_setloghandler(pdf_android_loghandler); */
    return JNI_VERSION_1_2;
}


/**
 * Implementation of native method PDF.parseFile.
 * Opens file and parses at least some bytes - so it could take a while.
 * @param file_name file name to parse.
 */
JNIEXPORT void JNICALL
Java_cx_hell_android_pdfview_PDF_parseFile(
        JNIEnv *env,
        jobject jthis,
        jstring file_name) {
    const char *c_file_name = NULL;
    jboolean iscopy;
    jclass this_class;
    jfieldID pdf_field_id;
    pdf_t *pdf = NULL;


    c_file_name = (*env)->GetStringUTFChars(env, file_name, &iscopy);
	this_class = (*env)->GetObjectClass(env, jthis);
	pdf_field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");
	pdf = parse_pdf_file(c_file_name, 0);
    (*env)->ReleaseStringUTFChars(env, file_name, c_file_name);
	(*env)->SetIntField(env, jthis, pdf_field_id, (int)pdf);
}


/**
 * Create pdf_t struct from opened file descriptor.
 */
JNIEXPORT void JNICALL
Java_cx_hell_android_pdfview_PDF_parseFileDescriptor(
        JNIEnv *env,
        jobject jthis,
        jobject fileDescriptor) {
    int fileno;
    jclass this_class;
    jfieldID pdf_field_id;
    pdf_t *pdf = NULL;

	this_class = (*env)->GetObjectClass(env, jthis);
	pdf_field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");
    fileno = get_descriptor_from_file_descriptor(env, fileDescriptor);
	pdf = parse_pdf_file(NULL, fileno);
	(*env)->SetIntField(env, jthis, pdf_field_id, (int)pdf);
}


/**
 * Implementation of native method PDF.getPageCount - return page count of this PDF file.
 * Returns -1 on error, eg if pdf_ptr is NULL.
 * @param env JNI Environment
 * @param this PDF object
 * @return page count or -1 on error
 */
JNIEXPORT jint JNICALL
Java_cx_hell_android_pdfview_PDF_getPageCount(
		JNIEnv *env,
		jobject this) {
	pdf_t *pdf = NULL;
    pdf = get_pdf_from_this(env, this);
	if (pdf == NULL) return -1;
	return pdf_getpagecount(pdf->xref);
}


JNIEXPORT jintArray JNICALL
Java_cx_hell_android_pdfview_PDF_renderPage(
        JNIEnv *env,
        jobject this,
        jint pageno,
        jint zoom,
        jint left,
        jint top,
        jint rotation,
        jobject size) {

    int blen;
    jint *buf; /* rendered page, freed before return, as bitmap */
    jintArray jints; /* return value */
    int *jbuf; /* pointer to internal jint */
    pdf_t *pdf; /* parsed pdf data, extracted from java's "this" object */
    int width, height;

    get_size(env, size, &width, &height);

    __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "jni renderPage(pageno: %d, zoom: %d, left: %d, right: %d, width: %d, height: %d) start",
            (int)pageno, (int)zoom,
            (int)left, (int)top,
            (int)width, (int)height);

    pdf = get_pdf_from_this(env, this);
    buf = get_page_image_bitmap(pdf, pageno, zoom, left, top, rotation, &blen, &width, &height);

    if (buf == NULL) return NULL;

    save_size(env, size, width, height);

    /* TODO: learn jni and avoid copying bytes ;) */
    jints = (*env)->NewIntArray(env, blen);
	jbuf = (*env)->GetIntArrayElements(env, jints, NULL);
    memcpy(jbuf, buf, blen);
    (*env)->ReleaseIntArrayElements(env, jints, jbuf, 0);
    free(buf);
    return jints;
}


JNIEXPORT jint JNICALL
Java_cx_hell_android_pdfview_PDF_getPageSize(
        JNIEnv *env,
        jobject this,
        jint pageno,
        jobject size) {
    int width, height, error;
    pdf_t *pdf;

    pdf = get_pdf_from_this(env, this);
    if (pdf == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "this.pdf is null");
        return 1;
    }

    error = get_page_size(pdf, pageno, &width, &height);
    if (error != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "get_page_size error: %d", (int)error);
        __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "fitz error is:\n%s", fz_errorbuf);
        return 2;
    }

    save_size(env, size, width, height);
    return 0;
}


/**
 * Free resources allocated in native code.
 */
JNIEXPORT void JNICALL
Java_cx_hell_android_pdfview_PDF_freeMemory(
        JNIEnv *env,
        jobject this) {
    pdf_t *pdf = NULL;
	jclass this_class = (*env)->GetObjectClass(env, this);
	jfieldID pdf_field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");

    __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "jni freeMemory()");
	pdf = (pdf_t*) (*env)->GetIntField(env, this, pdf_field_id);
	(*env)->SetIntField(env, this, pdf_field_id, 0);

    if (pdf->pages) {
        int i;
        int pagecount;
        pagecount = pdf_getpagecount(pdf->xref);
        for(i = 0; i < pagecount; ++i) {
            if (pdf->pages[i]) {
                pdf_droppage(pdf->pages[i]);
            }
        }
        free(pdf->pages);
        pdf->pages = NULL;
    }

    /*
    if (pdf->textlines) {
        int i;
        int pagecount;
        pagecount = pdf_getpagecount(pdf->xref);
        for(i = 0; i < pagecount; ++i) {
            if (pdf->textlines[i]) {
                pdf_droptextline(pdf->textlines[i]);
            }
        }
        free(pdf->textlines);
        pdf->textlines = NULL;
    }
    */

    if (pdf->drawcache) {
        fz_freeglyphcache(pdf->drawcache);
        pdf->drawcache = NULL;
    }

    /* pdf->fileno is dup()-ed in parse_pdf_fileno */
    if (pdf->fileno >= 0) close(pdf->fileno);
    free(pdf);
}


JNIEXPORT jobject JNICALL
Java_cx_hell_android_pdfview_PDF_find(
        JNIEnv *env,
        jobject this,
        jstring text,
        jint pageno) {
    pdf_t *pdf = NULL;
    char *ctext = NULL;
    jboolean is_copy;
    jobject results = NULL;
    pdf_page *page = NULL;
    fz_textspan *textspan = NULL, *ln = NULL;
    fz_device *dev = NULL;
    char *textlinechars;
    char *found = NULL;
    fz_error error = 0;
    jobject find_result = NULL;

    ctext = (char*)(*env)->GetStringUTFChars(env, text, &is_copy);
    if (ctext == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "text cannot be null");
        (*env)->ReleaseStringUTFChars(env, text, ctext);
        return NULL;
    }
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "find(%s)", ctext);
    pdf = get_pdf_from_this(env, this);
    page = get_page(pdf, pageno);

    /*
    error = pdf_loadtextfromtree(&textlines, page->tree, fz_identity());
    if (error) {
        __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "pdf_loadtextfromtree failed");
        (*env)->ReleaseStringUTFChars(env, text, ctext);
        return NULL;
    }
    */
    textspan = fz_newtextspan();
    dev = fz_newtextdevice(textspan);
    error = pdf_runcontentstream(dev, fz_identity(), pdf->xref, page->resources, page->contents);

    for(ln = textspan; ln; ln = ln->next) {
        textlinechars = (char*)malloc(ln->len + 1);
        {
            int i;
            for(i = 0; i < ln->len; ++i) textlinechars[i] = ln->text[i].c;
        }
        textlinechars[ln->len] = 0;
        found = strcasestr(textlinechars, ctext);
        if (found) {
            __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "found something, creating empty find result");
            find_result = create_find_result(env);
            if (find_result == NULL) {
                __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "tried to create empty find result, but got NULL instead");
                /* TODO: free resources */
                (*env)->ReleaseStringUTFChars(env, text, ctext);
                return;
            }
            __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "found something, empty find result created");
            set_find_result_page(env, find_result, pageno);
            /* now add markers to this find result */
            {
                int i = 0;
                int i0, i1;
                /* int x, y; */
                fz_bbox charbox;
                i0 = (found-textlinechars);
                i1 = i0 + strlen(ctext);
                for(i = i0; i < i1; ++i) {
                    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "adding marker for letter %d: %c", i, textlinechars[i]);
                    /* 
                    x = ln->text[i].x;
                    y = ln->text[i].y;
                    convert_point_pdf_to_apv(pdf, pageno, &x, &y);
                    */
                    charbox = ln->text[i].bbox;
                    convert_box_pdf_to_apv(pdf, pageno, &charbox);
                    /* add_find_result_marker(env, find_result, x-2, y-2, x+2, y+2); */
                    add_find_result_marker(env, find_result, charbox.x0-2, charbox.y0-2, charbox.x1+2, charbox.y1+2); /* TODO: check errors */

                }
                /* TODO: obviously this sucks massively, good God please forgive me for writing this; if only I had more time... */
                /*
                x = ((float)(ln->text[i1-1].x - ln->text[i0].x)) / (float)strlen(ctext) + ln->text[i1-1].x;
                y = ((float)(ln->text[i1-1].y - ln->text[i0].y)) / (float)strlen(ctext) + ln->text[i1-1].y;
                convert_point_pdf_to_apv(pdf, pageno, &x, &y);
                __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "adding final marker");
                add_find_result_marker(env,
                        find_result,
                        x-2, y-2,
                        x+2, y+2
                    );
                */
            }
            __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "adding find result to list");
            add_find_result_to_list(env, &results, find_result);
            __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "added find result to list");
        }
        free(textlinechars);
    }

    fz_freedevice(dev);
    fz_freetextspan(textspan);

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "releasing text back to jvm");
    (*env)->ReleaseStringUTFChars(env, text, ctext);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "returning results");
    return results;
}



/**
 * Create empty FindResult object.
 * @param env JNI Environment
 * @return newly created, empty FindResult object
 */
jobject create_find_result(JNIEnv *env) {
    static jmethodID constructorID;
    jclass findResultClass = NULL;
    static int jni_ids_cached = 0;
    jobject findResultObject = NULL;

    findResultClass = (*env)->FindClass(env, "cx/hell/android/lib/pagesview/FindResult");

    if (findResultClass == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_find_result: FindClass returned NULL");
        return NULL;
    }

    if (jni_ids_cached == 0) {
        constructorID = (*env)->GetMethodID(env, findResultClass, "<init>", "()V");
        if (constructorID == NULL) {
            (*env)->DeleteLocalRef(env, findResultClass);
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_find_result: couldn't get method id for FindResult constructor");
            return NULL;
        }
        jni_ids_cached = 1;
    }

    findResultObject = (*env)->NewObject(env, findResultClass, constructorID);
    return findResultObject;
}


void add_find_result_to_list(JNIEnv *env, jobject *list, jobject find_result) {
    static int jni_ids_cached = 0;
    static jmethodID list_add_method_id = NULL;
    jclass list_class = NULL;
    if (list == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "list cannot be null - it must be a pointer jobject variable");
        return;
    }
    if (find_result == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "find_result cannot be null");
        return;
    }
    if (*list == NULL) {
        jmethodID list_constructor_id;
        __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "creating ArrayList");
        list_class = (*env)->FindClass(env, "java/util/ArrayList");
        if (list_class == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "couldn't find class java/util/ArrayList");
            return;
        }
        list_constructor_id = (*env)->GetMethodID(env, list_class, "<init>", "()V");
        if (!list_constructor_id) {
            __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "couldn't find ArrayList constructor");
            return;
        }
        *list = (*env)->NewObject(env, list_class, list_constructor_id);
        if (*list == NULL) {
            __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "failed to create ArrayList: NewObject returned NULL");
            return;
        }
    }

    if (!jni_ids_cached) {
        if (list_class == NULL) {
            list_class = (*env)->FindClass(env, "java/util/ArrayList");
            if (list_class == NULL) {
                __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "couldn't find class java/util/ArrayList");
                return;
            }
        }
        list_add_method_id = (*env)->GetMethodID(env, list_class, "add", "(Ljava/lang/Object;)Z");
        if (list_add_method_id == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "couldn't get ArrayList.add method id");
            return;
        }
        jni_ids_cached = 1;
    } 

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "calling ArrayList.add");
    (*env)->CallBooleanMethod(env, *list, list_add_method_id, find_result);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "add_find_result_to_list done");
}


/**
 * Set find results page member.
 * @param JNI environment
 * @param findResult find result object that should be modified
 * @param page new value for page field
 */
void set_find_result_page(JNIEnv *env, jobject findResult, int page) {
    static char jni_ids_cached = 0;
    static jfieldID page_field_id = 0;
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "trying to set find results page number");
    if (jni_ids_cached == 0) {
        jclass findResultClass = (*env)->GetObjectClass(env, findResult);
        page_field_id = (*env)->GetFieldID(env, findResultClass, "page", "I");
        jni_ids_cached = 1;
    }
    (*env)->SetIntField(env, findResult, page_field_id, page);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "find result page number set");
}


/**
 * Add marker to find result.
 */
void add_find_result_marker(JNIEnv *env, jobject findResult, int x0, int y0, int x1, int y1) {
    static jmethodID addMarker_methodID = 0;
    static unsigned char jni_ids_cached = 0;
    if (!jni_ids_cached) {
        jclass findResultClass = NULL;
        findResultClass = (*env)->FindClass(env, "cx/hell/android/lib/pagesview/FindResult");
        if (findResultClass == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "add_find_result_marker: FindClass returned NULL");
            return;
        }
        addMarker_methodID = (*env)->GetMethodID(env, findResultClass, "addMarker", "(IIII)V");
        if (addMarker_methodID == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "add_find_result_marker: couldn't find FindResult.addMarker method ID");
            return;
        }
        jni_ids_cached = 1;
    }
    (*env)->CallVoidMethod(env, findResult, addMarker_methodID, x0, y0, x1, y1); /* TODO: is always really int jint? */
}


/**
 * Get pdf_ptr field value, cache field address as a static field.
 * @param env Java JNI Environment
 * @param this object to get "pdf_ptr" field from
 * @return pdf_ptr field value
 */
pdf_t* get_pdf_from_this(JNIEnv *env, jobject this) {
    static jfieldID field_id = 0;
    static unsigned char field_is_cached = 0;
    pdf_t *pdf = NULL;
    if (! field_is_cached) {
        jclass this_class = (*env)->GetObjectClass(env, this);
        field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");
        field_is_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached pdf_ptr field id %d", (int)field_id);
    }
	pdf = (pdf_t*) (*env)->GetIntField(env, this, field_id);
    return pdf;
}


/**
 * Get descriptor field value from FileDescriptor class, cache field offset.
 * This is undocumented private field.
 * @param env JNI Environment
 * @param this FileDescriptor object
 * @return file descriptor field value
 */
int get_descriptor_from_file_descriptor(JNIEnv *env, jobject this) {
    static jfieldID field_id = 0;
    static unsigned char is_cached = 0;
    if (!is_cached) {
        jclass this_class = (*env)->GetObjectClass(env, this);
        field_id = (*env)->GetFieldID(env, this_class, "descriptor", "I");
        is_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached descriptor field id %d", (int)field_id);
    }
    __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "will get descriptor field...");
    return (*env)->GetIntField(env, this, field_id);
}


void get_size(JNIEnv *env, jobject size, int *width, int *height) {
    static jfieldID width_field_id = 0;
    static jfieldID height_field_id = 0;
    static unsigned char fields_are_cached = 0;
    if (! fields_are_cached) {
        jclass size_class = (*env)->GetObjectClass(env, size);
        width_field_id = (*env)->GetFieldID(env, size_class, "width", "I");
        height_field_id = (*env)->GetFieldID(env, size_class, "height", "I");
        fields_are_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached Size fields");
    }
    *width = (*env)->GetIntField(env, size, width_field_id);
    *height = (*env)->GetIntField(env, size, height_field_id);
}


/**
 * Store width and height values into PDF.Size object, cache field ids in static members.
 * @param env JNI Environment
 * @param width width to store
 * @param height height field value to be stored
 * @param size target PDF.Size object
 */
void save_size(JNIEnv *env, jobject size, int width, int height) {
    static jfieldID width_field_id = 0;
    static jfieldID height_field_id = 0;
    static unsigned char fields_are_cached = 0;
    if (! fields_are_cached) {
        jclass size_class = (*env)->GetObjectClass(env, size);
        width_field_id = (*env)->GetFieldID(env, size_class, "width", "I");
        height_field_id = (*env)->GetFieldID(env, size_class, "height", "I");
        fields_are_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached Size fields");
    }
    (*env)->SetIntField(env, size, width_field_id, width);
    (*env)->SetIntField(env, size, height_field_id, height);
}


/**
 * pdf_t "constructor": create empty pdf_t with default values.
 * @return newly allocated pdf_t struct with fields set to default values
 */
pdf_t* create_pdf_t() {
    pdf_t *pdf = NULL;
    pdf = (pdf_t*)malloc(sizeof(pdf_t));
    pdf->xref = NULL;
    pdf->outline = NULL;
    pdf->fileno = -1;
    pdf->pages = NULL;
    pdf->drawcache = NULL;
}


#if 0
/**
 * Parse bytes into PDF struct.
 * @param bytes pointer to bytes that should be parsed
 * @param len length of byte buffer
 * @return initialized pdf_t struct; or NULL if loading failed
 */
pdf_t* parse_pdf_bytes(unsigned char *bytes, size_t len) {
    pdf_t *pdf;
    fz_error error;

    pdf = create_pdf_t();

    pdf->xref = pdf_newxref();
    error = pdf_loadxref_mem(pdf->xref, bytes, len);
    if (error) {
        __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "got err from pdf_loadxref_mem: %d", (int)error);
        __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "fz errors:\n%s", fz_errorbuf);
        /* TODO: free resources */
        return NULL;
    }

    error = pdf_decryptxref(pdf->xref);
    if (error) {
        return NULL;
    }

    if (pdf_needspassword(pdf->xref)) {
        int authenticated = 0;
        authenticated = pdf_authenticatepassword(pdf->xref, "");
        if (!authenticated) {
            /* TODO: ask for password */
            __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "failed to authenticate with empty password");
            return NULL;
        }
    }

    pdf->xref->root = fz_resolveindirect(fz_dictgets(pdf->xref->trailer, "Root"));
    fz_keepobj(pdf->xref->root);

    pdf->xref->info = fz_resolveindirect(fz_dictgets(pdf->xref->trailer, "Info"));
    fz_keepobj(pdf->xref->info);

    pdf->outline = pdf_loadoutline(pdf->xref);

    return pdf;
}
#endif


/**
 * Parse file into PDF struct.
 * Use filename if it's not null, otherwise use fileno.
 */
pdf_t* parse_pdf_file(const char *filename, int fileno) {
    pdf_t *pdf;
    fz_error error;
    int fd;
    fz_stream *file;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "parse_pdf_file(%s, %d)", filename, fileno);

    pdf = create_pdf_t();

    if (filename) {
        fd = open(filename, O_BINARY | O_RDONLY, 0666);
        if (fd < 0) {
            return NULL;
        }
    } else {
        pdf->fileno = dup(fileno);
        fd = pdf->fileno;
    }

    file = fz_openfile(fd);
    pdf->xref = pdf_openxref(file);
    if (!pdf->xref) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "got NULL from pdf_openxref");
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "fz errors:\n%s", fz_errorbuf);
                return NULL;
    }

    /*
    error = pdf_decryptxref(pdf->xref);
    if (error) {
        return NULL;
    }
    */

    if (pdf_needspassword(pdf->xref)) {
        int authenticated = 0;
        authenticated = pdf_authenticatepassword(pdf->xref, "");
        if (!authenticated) {
            /* TODO: ask for password */
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "failed to authenticate with empty password");
            return NULL;
        }
    }

    /* pdf->xref->root = fz_resolveindirect(fz_dictgets(pdf->xref->trailer, "Root"));
    fz_keepobj(pdf->xref->root);
    pdf->xref->info = fz_resolveindirect(fz_dictgets(pdf->xref->trailer, "Info"));
    if (pdf->xref->info) fz_keepobj(pdf->xref->info);
    */
    pdf->outline = pdf_loadoutline(pdf->xref);
    return pdf;
}


/**
 * Calculate zoom to best match given dimensions.
 * There's no guarantee that page zoomed by resulting zoom will fit rectangle max_width x max_height exactly.
 * @param max_width expected max width
 * @param max_height expected max height
 * @param page original page
 * @return zoom required to best fit page into max_width x max_height rectangle
 */
double get_page_zoom(pdf_page *page, int max_width, int max_height) {
    double page_width, page_height;
    double zoom_x, zoom_y;
    double zoom;
    page_width = page->mediabox.x1 - page->mediabox.x0;
    page_height = page->mediabox.y1 - page->mediabox.y0;

    zoom_x = max_width / page_width;
    zoom_y = max_height / page_height;

    zoom = (zoom_x < zoom_y) ? zoom_x : zoom_y;

    return zoom;
}


/**
 * Lazy get-or-load page.
 * Only PDFVIEW_MAX_PAGES_LOADED pages can be loaded at the time.
 * @param pdf pdf struct
 * @param pageno 0-based page number
 * @return pdf_page
 */
pdf_page* get_page(pdf_t *pdf, int pageno) {
    fz_error error = 0;
    int loaded_pages = 0;
    int pagecount;

    pagecount = pdf_getpagecount(pdf->xref);

    if (!pdf->pages) {
        int i;
        pdf->pages = (pdf_page**)malloc(pagecount * sizeof(pdf_page*));
        for(i = 0; i < pagecount; ++i) pdf->pages[i] = NULL;
    }

    if (!pdf->pages[pageno]) {
        pdf_page *page = NULL;
        fz_obj *obj = NULL;
        int loaded_pages = 0;
        int i = 0;

        for(i = 0; i < pagecount; ++i) {
            if (pdf->pages[i]) loaded_pages++;
        }

        if (loaded_pages >= PDFVIEW_MAX_PAGES_LOADED) {
            int page_to_drop = 0; /* not the page number */
            int j = 0;
            __android_log_print(ANDROID_LOG_INFO, PDFVIEW_LOG_TAG, "already loaded %d pages, going to drop random one", loaded_pages);
            page_to_drop = rand() % loaded_pages;
            __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "will drop %d-th loaded page", page_to_drop);
            /* search for page_to_drop-th loaded page and then drop it */
            for(i = 0; i < pagecount; ++i) {
                if (pdf->pages[i]) {
                    /* one of loaded pages, the j-th one */
                    if (j == page_to_drop) {
                        __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "found %d-th loaded page, it's %d-th in document, dropping now", page_to_drop, i);
                        pdf_droppage(pdf->pages[i]);
                        pdf->pages[i] = NULL;
                        break;
                    } else {
                        j++;
                    }
                }
            }
        }

        obj = pdf_getpageobject(pdf->xref, pageno+1);
        error = pdf_loadpage(&page, pdf->xref, obj);
        if (error) {
            __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "pdf_loadpage -> %d", (int)error);
            __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "fitz error is:\n%s", fz_errorbuf);
            return NULL;
        }
        pdf->pages[pageno] = page;
    }
    return pdf->pages[pageno];
}


/**
 * Get part of page as bitmap.
 * Parameters left, top, width and height are interprted after scalling, so if we have 100x200 page scalled by 25% and
 * request 0x0 x 25x50 tile, we should get 25x50 bitmap of whole page content.
 * Page size is currently MediaBox size: http://www.prepressure.com/pdf/basics/page_boxes, but probably shuld be TrimBox.
 * pageno is 0-based.
 */
jint* get_page_image_bitmap(pdf_t *pdf, int pageno, int zoom_pmil, int left, int top, int rotation, int *blen, int *width, int *height) {
    unsigned char *bytes = NULL;
    fz_matrix ctm;
    double zoom;
    fz_rect bbox;
    fz_error error = 0;
    pdf_page *page = NULL;
    fz_pixmap *image = NULL;
    static int runs = 0;
    fz_device *dev = NULL;

    zoom = (double)zoom_pmil / 1000.0;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "get_page_image_bitmap(pageno: %d) start", (int)pageno);

    if (!pdf->drawcache) {
        pdf->drawcache = fz_newglyphcache();
        if (!pdf->drawcache) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "failed to create glyphcache");
            return NULL;
        }
    }

    pdf_flushxref(pdf->xref, 0);

    page = get_page(pdf, pageno);
    if (!page) return NULL; /* TODO: handle/propagate errors */

    ctm = fz_identity();
    ctm = fz_concat(ctm, fz_translate(-page->mediabox.x0, -page->mediabox.y1));
    ctm = fz_concat(ctm, fz_scale(zoom, -zoom));
    rotation = page->rotate + rotation * -90;
    if (rotation != 0) ctm = fz_concat(ctm, fz_rotate(rotation));
    bbox = fz_transformrect(ctm, page->mediabox);

    /* not bbox holds page after transform, but we only need tile at (left,right) from top-left corner */

    bbox.x0 = bbox.x0 + left;
    bbox.y0 = bbox.y0 + top;
    bbox.x1 = bbox.x0 + *width;
    bbox.y1 = bbox.y0 + *height;


#if 0
    error = fz_rendertree(&image, pdf->renderer, page->tree, ctm, fz_roundrect(bbox), 1);
    if (error) {
        fz_rethrow(error, "rendering failed");
        /* TODO: cleanup mem on error, so user can try to open many files without causing memleaks; also report errors nicely to user */
        return NULL;
    }
#endif

    image = fz_newpixmap(pdf_devicergb, bbox.x0, bbox.y0, *width, *height);
    fz_clearpixmap(image, 0xff);
    memset(image->samples, 0xff, image->h * image->w * image->n);
    dev = fz_newdrawdevice(pdf->drawcache, image);
    error = pdf_runcontentstream(dev, ctm, pdf->xref, page->resources, page->contents);
    if (error) {
        __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "pdf_runcontentstream failed: %s", (int)error);
        /* TODO: free resources, report errors */
        return NULL;
    }
    fz_freedevice(dev);

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got image %d x %d, asked for %d x %d",
            (int)(image->w), (int)(image->h),
            *width, *height);

    fix_samples(image->samples, image->w, image->h);

    /* TODO: shouldn't malloc so often; but in practice, those temp malloc-memcpy pairs don't cost that much */
    bytes = (unsigned char*)malloc(image->w * image->h * 4);
    memcpy(bytes, image->samples, image->w * image->h * 4);
    *blen = image->w * image->h * 4;
    *width = image->w;
    *height = image->h;
    fz_droppixmap(image);

    runs += 1;
    return (jint*)bytes;
}


/**
 * Reorder bytes in image data - convert from mupdf image to android image.
 * TODO: make it portable across different architectures (when they're released).
 * TODO: make mupdf write pixels in correct format
 */
void fix_samples(unsigned char *bytes, unsigned int w, unsigned int h) {
        unsigned char r,g,b,a;
        unsigned i = 0;
        for (i = 0; i < (w*h); ++i) {
                unsigned int o = i*4;
                a = bytes[o+0];
                r = bytes[o+1];
                g = bytes[o+2];
                b = bytes[o+3];
                bytes[o+0] = b; /* b */
                bytes[o+1] = g; /* g */
                bytes[o+2] = r; /* r */
                bytes[o+3] = a;
        }
}


/**
 * Get page size in APV's convention.
 * @param page 0-based page number
 * @param pdf pdf struct
 * @param width target for width value
 * @param height target for height value
 * @return error code - 0 means ok
 */
int get_page_size(pdf_t *pdf, int pageno, int *width, int *height) {
    fz_error error = 0;
    fz_obj *pageobj = NULL;
    fz_obj *sizeobj = NULL;
    fz_rect bbox;
    fz_obj *rotateobj = NULL;
    int rotate = 0;

    pageobj = pdf_getpageobject(pdf->xref, pageno+1);
    sizeobj = fz_dictgets(pageobj, "MediaBox");
    rotateobj = fz_dictgets(pageobj, "Rotate");
    if (fz_isint(rotateobj)) {
        rotate = fz_toint(rotateobj);
    } else {
        rotate = 0;
    }
    bbox = pdf_torect(sizeobj);
    if (rotate != 0 && (rotate % 180) == 90) {
        *width = bbox.y1 - bbox.y0;
        *height = bbox.x1 - bbox.x0;
    } else {
        *width = bbox.x1 - bbox.x0;
        *height = bbox.y1 - bbox.y0;
    }
    return 0;
}


#if 0
/**
 * Convert coordinates from pdf to APVs.
 * TODO: faster? lazy?
 * @return error code, 0 means ok
 */
int convert_point_pdf_to_apv(pdf_t *pdf, int page, int *x, int *y) {
    fz_error error = 0;
    fz_obj *pageobj = NULL;
    fz_obj *rotateobj = NULL;
    fz_obj *sizeobj = NULL;
    fz_rect bbox;
    int rotate = 0;
    fz_point p;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "convert_point_pdf_to_apv()");

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "trying to convert %d x %d to APV coords", *x, *y);

    pageobj = pdf_getpageobject(pdf->xref, page+1);
    if (!pageobj) return -1;
    sizeobj = fz_dictgets(pageobj, "MediaBox");
    if (!sizeobj) return -1;
    bbox = pdf_torect(sizeobj);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "page bbox is %.1f, %.1f, %.1f, %.1f", bbox.x0, bbox.y0, bbox.x1, bbox.y1);
    rotateobj = fz_dictgets(pageobj, "Rotate");
    if (fz_isint(rotateobj)) {
        rotate = fz_toint(rotateobj);
    } else {
        rotate = 0;
    }
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "rotate is %d", (int)rotate);

    p.x = *x;
    p.y = *y;

    if (rotate != 0) {
        fz_matrix m;
        m = fz_rotate(-rotate);
        bbox = fz_transformrect(m, bbox);
        p = fz_transformpoint(m, p);
    }

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "after rotate bbox is: %.1f, %.1f, %.1f, %.1f", bbox.x0, bbox.y0, bbox.x1, bbox.y1);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "after rotate point is: %.1f, %.1f", p.x, p.y);

    *x = p.x - MIN(bbox.x0,bbox.x1);
    *y = MAX(bbox.y1, bbox.y0) - p.y;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "result is: %d, %d", *x, *y);

    return 0;
}
#endif


/**
 * Convert coordinates from pdf to APV.
 * Result is stored in location pointed to by bbox param.
 * This function has to get page MediaBox relative to which bbox is located.
 * This function should not allocate any memory.
 * @return error code, 0 means ok
 */
int convert_box_pdf_to_apv(pdf_t *pdf, int page, fz_bbox *bbox) {
    fz_error error = 0;
    fz_obj *pageobj = NULL;
    fz_obj *rotateobj = NULL;
    fz_obj *sizeobj = NULL;
    fz_rect page_bbox;
    fz_rect param_bbox;
    int rotate = 0;
    float height = 0;
    float width = 0;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "convert_box_pdf_to_apv(page: %d, bbox: %d %d %d %d)", page, bbox->x0, bbox->y0, bbox->x1, bbox->y1);

    /* copying field by field becuse param_bbox is fz_rect (floats) and *bbox is fz_bbox (ints) */
    param_bbox.x0 = bbox->x0;
    param_bbox.y0 = bbox->y0;
    param_bbox.x1 = bbox->x1;
    param_bbox.y1 = bbox->y1;

    pageobj = pdf_getpageobject(pdf->xref, page+1);
    if (!pageobj) return -1;
    sizeobj = fz_dictgets(pageobj, "MediaBox");
    if (!sizeobj) return -1;
    page_bbox = pdf_torect(sizeobj);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "page bbox is %.1f, %.1f, %.1f, %.1f", page_bbox.x0, page_bbox.y0, page_bbox.x1, page_bbox.y1);
    rotateobj = fz_dictgets(pageobj, "Rotate");
    if (fz_isint(rotateobj)) {
        rotate = fz_toint(rotateobj);
    } else {
        rotate = 0;
    }
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "rotate is %d", (int)rotate);

    if (rotate != 0) {
        fz_matrix m;
        m = fz_rotate(-rotate);
        param_bbox = fz_transformrect(m, param_bbox);
        page_bbox = fz_transformrect(m, page_bbox);
    }

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "after rotate page bbox is: %.1f, %.1f, %.1f, %.1f", page_bbox.x0, page_bbox.y0, page_bbox.x1, page_bbox.y1);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "after rotate param bbox is: %.1f, %.1f, %.1f, %.1f", param_bbox.x0, param_bbox.y0, param_bbox.x1, param_bbox.y1);

    /* set result: param bounding box relative to left-top corner of page bounding box */

    /*
    bbox->x0 = MIN(param_bbox.x0, param_bbox.x1) - MIN(page_bbox.x0, page_bbox.x1);
    bbox->y0 = MIN(param_bbox.y0, param_bbox.y1) - MIN(page_bbox.y0, page_bbox.y1);
    bbox->x1 = MAX(param_bbox.x0, param_bbox.x1) - MIN(page_bbox.x0, page_bbox.x1);
    bbox->y1 = MAX(param_bbox.y0, param_bbox.y1) - MIN(page_bbox.y0, page_bbox.y1);
    */

    width = ABS(page_bbox.x0 - page_bbox.x1);
    height = ABS(page_bbox.y0 - page_bbox.y1);

    bbox->x0 = (MIN(param_bbox.x0, param_bbox.x1) - MIN(page_bbox.x0, page_bbox.x1));
    bbox->y1 = height - (MIN(param_bbox.y0, param_bbox.y1) - MIN(page_bbox.y0, page_bbox.y1));
    bbox->x1 = (MAX(param_bbox.x0, param_bbox.x1) - MIN(page_bbox.x0, page_bbox.x1));
    bbox->y0 = height - (MAX(param_bbox.y0, param_bbox.y1) - MIN(page_bbox.y0, page_bbox.y1));

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "result after transformations: %d, %d, %d, %d", bbox->x0, bbox->y0, bbox->x1, bbox->y1);

    return 0;
}


void pdf_android_loghandler(const char *m) {
    __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview.mupdf", m);
}



/* vim: set sts=4 ts=4 sw=4 et: */