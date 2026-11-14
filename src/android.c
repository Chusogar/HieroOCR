/*
 * Punto de entrada y servicios de plataforma en Android.
 *
 * La actividad Java (android/app/src/main/java/org/hieroocr/app/HieroActivity.java)
 * copia los datos a la memoria interna, abre el selector de archivos o la
 * cámara, rasteriza los PDF con PdfRenderer y endereza las fotos según su EXIF.
 * Cuando tiene las imágenes listas llama a nativeRequest(), que las pasa a la
 * interfaz SDL como un evento.
 */
#ifdef __ANDROID__

#include "hieroocr.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <android/log.h>
#include <jni.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile Uint32 ev_type = 0;   /* 0 hasta que la interfaz esté lista */
static char mdc_path[1024] = "salida.mdc";

/* ---------- llamadas de C a Java ---------- */

static void call_java(const char *method, const char *sig, ...)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act)
        return;
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, method, sig);
    if (m) {
        va_list ap;
        va_start(ap, sig);
        (*env)->CallStaticVoidMethodV(env, cls, m, ap);
        va_end(ap);
    }
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
}

int platform_is_mobile(void) { return 1; }
void platform_pick_file(void) { call_java("pickFile", "()V"); }
void platform_take_photo(void) { call_java("takePhoto", "()V"); }
const char *platform_mdc_path(const Options *o) { (void)o; return mdc_path; }

void platform_share(const char *path, const char *text)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    if (!env)
        return;
    jstring jp = (*env)->NewStringUTF(env, path);
    jstring jt = (*env)->NewStringUTF(env, text);
    call_java("shareText", "(Ljava/lang/String;Ljava/lang/String;)V", jp, jt);
    (*env)->DeleteLocalRef(env, jp);
    (*env)->DeleteLocalRef(env, jt);
}

/* ---------- llamadas de Java a C (desde un hilo de trabajo de Java) ---------- */

JNIEXPORT void JNICALL
Java_org_hieroocr_app_HieroActivity_nativeRequest(JNIEnv *env, jclass cls, jint kind,
                                                  jobjectArray files, jstring msg)
{
    (void)cls;
    for (int i = 0; i < 400 && ev_type == 0; i++)   /* la interfaz aún arranca */
        SDL_Delay(50);
    if (ev_type == 0)
        return;
    Request *r = calloc(1, sizeof *r);
    if (!r)
        return;
    r->kind = kind;
    if (msg) {
        const char *s = (*env)->GetStringUTFChars(env, msg, NULL);
        if (s) {
            snprintf(r->msg, sizeof r->msg, "%s", s);
            (*env)->ReleaseStringUTFChars(env, msg, s);
        }
    }
    int n = files ? (*env)->GetArrayLength(env, files) : 0;
    if (n > 0) {
        r->files = calloc((size_t)n, sizeof(char *));
        for (int i = 0; i < n; i++) {
            jstring js = (jstring)(*env)->GetObjectArrayElement(env, files, i);
            const char *s = js ? (*env)->GetStringUTFChars(env, js, NULL) : NULL;
            if (s) {
                r->files[r->n++] = strdup(s);
                (*env)->ReleaseStringUTFChars(env, js, s);
            }
            if (js)
                (*env)->DeleteLocalRef(env, js);
        }
    }
    SDL_Event e;
    SDL_zero(e);
    e.type = ev_type;
    e.user.data1 = r;
    if (SDL_PushEvent(&e) != 1)
        request_free(r);
}

/* ---------- programa ---------- */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    /* Los datos (modelo, lista de signos, fuente) están en la memoria interna,
     * donde los copia HieroActivity: se trabaja desde allí con rutas relativas. */
    const char *home = SDL_AndroidGetInternalStoragePath();
    if (home && chdir(home) != 0)
        __android_log_print(ANDROID_LOG_WARN, "hieroocr", "chdir(%s) ha fallado", home);
    if (home)
        snprintf(mdc_path, sizeof mdc_path, "%s/salida.mdc", home);
    /* En Android stderr no va a ninguna parte: los mensajes se guardan en registro.txt. */
    if (freopen("registro.txt", "w", stderr))
        setvbuf(stderr, NULL, _IONBF, 0);

    Options o = {
        .dpi = 300, .uncertain_thr = 0.11f, .tmpl_dir = "plantillas", .gui = 1,
    };
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "hieroocr", "SDL_Init: %s", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    if (TTF_Init() != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "hieroocr", "TTF_Init: %s", TTF_GetError());
        return 1;
    }
    ev_type = gui_event_type();

    app_find_data(&o);
    classify_set_options(&o);
    TemplateSet ts;
    app_load_templates(&o, &ts, 0);
    __android_log_print(ANDROID_LOG_INFO, "hieroocr", "Modelo %s: %d plantillas",
                        o.model ? o.model : "(ninguno)", ts.n);

    Doc doc = {0};
    int rc = gui_run(&doc, &o, &ts);
    doc_free(&doc);
    ts_free(&ts);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return rc;
}

#endif /* __ANDROID__ */
