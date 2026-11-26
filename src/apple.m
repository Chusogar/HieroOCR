/*
 * Servicios de plataforma en macOS e iOS.
 *
 * macOS: el programa es el de escritorio (main.c); aquí están el selector de
 * archivos (NSOpenPanel, el archivo llega a la interfaz como si se hubiera
 * soltado en la ventana), la carpeta de trabajo al abrirla desde el Finder
 * (Documentos/HieroOCR) y los PDF rasterizados con CoreGraphics.
 *
 * iOS: como android.c. El programa empieza aquí (SDL_main); los datos se
 * copian del paquete a Application Support; «Abrir» ofrece la fototeca o
 * Archivos (imágenes, PDF o un modelo .hmod), «Cámara» la cámara; las fotos se
 * enderezan y reducen, los PDF se rasterizan con CoreGraphics (preguntando las
 * páginas si hay varias) y todo llega a la interfaz como un Request. El MdC se
 * guarda en Documentos (visible en la app Archivos) y se comparte con la hoja
 * de compartir de iOS.
 */
#ifdef __APPLE__

#include "hieroocr.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_syswm.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

/* ---------- PDF con CoreGraphics (macOS e iOS) ---------- */

#if HIERO_IOS
#define PDF_MAX_SIDE 4000   /* píxeles: como en Android, sin agotar la memoria del móvil */
#else
#define PDF_MAX_SIDE 0      /* sin límite */
#endif

static int write_png(CGImageRef img, const char *path)
{
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    CGImageDestinationRef d = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, NULL);
    if (!d)
        return -1;
    CGImageDestinationAddImage(d, img, NULL);
    int ok = CGImageDestinationFinalize(d);
    CFRelease(d);
    return ok ? 0 : -1;
}

/* Una página a PNG, en blanco y negro (escala de grises), girada como manda el PDF. */
static int render_pdf_page(CGPDFPageRef page, int dpi, const char *out)
{
    CGRect box = CGPDFPageGetBoxRect(page, kCGPDFCropBox);
    int rot = CGPDFPageGetRotationAngle(page) % 360;
    if (rot < 0)
        rot += 360;
    double pw = box.size.width, ph = box.size.height;
    if (rot == 90 || rot == 270) {
        double t = pw;
        pw = ph;
        ph = t;
    }
    double s = dpi / 72.0;
    if (PDF_MAX_SIDE > 0 && (pw > ph ? pw : ph) * s > PDF_MAX_SIDE)
        s = PDF_MAX_SIDE / (pw > ph ? pw : ph);
    size_t w = (size_t)(pw * s + 0.5), h = (size_t)(ph * s + 0.5);
    if (w < 1 || h < 1 || (double)w * h > 200e6)
        return -1;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = CGBitmapContextCreate(NULL, w, h, 8, 0, cs, (CGBitmapInfo)kCGImageAlphaNone);
    CGColorSpaceRelease(cs);
    if (!ctx)
        return -1;
    CGContextSetGrayFillColor(ctx, 1.0, 1.0);
    CGContextFillRect(ctx, CGRectMake(0, 0, w, h));
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextSetRenderingIntent(ctx, kCGRenderingIntentDefault);
    /* la página ocupa el mapa de bits entero, con su giro */
    CGAffineTransform t = CGPDFPageGetDrawingTransform(page, kCGPDFCropBox, CGRectMake(0, 0, pw, ph), 0, true);
    CGContextScaleCTM(ctx, (CGFloat)(w / pw), (CGFloat)(h / ph));
    CGContextConcatCTM(ctx, t);
    CGContextClipToRect(ctx, box);
    CGContextDrawPDFPage(ctx, page);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (!img)
        return -1;
    int rc = write_png(img, out);
    CGImageRelease(img);
    return rc;
}

static CGPDFDocumentRef open_pdf(const char *pdf)
{
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:pdf]];
    CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL((__bridge CFURLRef)url);
    if (doc && CGPDFDocumentIsEncrypted(doc) && !CGPDFDocumentUnlockWithPassword(doc, "")) {
        CGPDFDocumentRelease(doc);
        return NULL;
    }
    return doc;
}

int apple_pdf_render(const char *pdf, int dpi, int (*ranges)[2], int nr, const char *tmpdir)
{
    @autoreleasepool {
        CGPDFDocumentRef doc = open_pdf(pdf);
        if (!doc) {
            fprintf(stderr, "CoreGraphics no puede abrir %s\n", pdf);
            return -1;
        }
        int np = (int)CGPDFDocumentGetNumberOfPages(doc), done = 0;
        fprintf(stderr, "%s: %d páginas (CoreGraphics)\n", pdf, np);
        for (int r = 0; r < nr; r++) {
            int a = ranges[r][0] > 0 ? ranges[r][0] : 1, b = ranges[r][1] > 0 ? ranges[r][1] : np;
            if (a > np)
                fprintf(stderr, "Aviso: el PDF sólo tiene %d páginas (se pidió la %d)\n", np, a);
            for (int pg = a; pg <= b && pg <= np; pg++) {
                @autoreleasepool {
                    CGPDFPageRef page = CGPDFDocumentGetPage(doc, (size_t)pg);
                    if (!page)
                        continue;
                    char out[700];
                    snprintf(out, sizeof out, "%s/p-%03d.png", tmpdir, pg);
                    fprintf(stderr, "Rasterizando la página %d a %d ppp...\n", pg, dpi);
                    if (render_pdf_page(page, dpi, out) == 0)
                        done++;
                }
            }
        }
        CGPDFDocumentRelease(doc);
        return done > 0 ? 0 : -1;
    }
}

#if !HIERO_IOS
/* =====================================================================
 * macOS
 * ===================================================================== */
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

void apple_startup(void)
{
    /* Desde el Finder la carpeta actual es «/» (no se puede escribir): las
     * plantillas aprendidas y el MdC van a Documentos/HieroOCR. Desde la
     * terminal se respeta la carpeta actual. */
    char cwd[1024];
    if (getcwd(cwd, sizeof cwd) && strcmp(cwd, "/") == 0) {
        NSString *docs = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
        NSString *dir = [docs stringByAppendingPathComponent:@"HieroOCR"];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        if (chdir(dir.fileSystemRepresentation) != 0)
            fprintf(stderr, "No se puede trabajar en %s\n", dir.UTF8String);
    }
}

int platform_is_mobile(void) { return getenv("HIEROOCR_SIMULAR_MOVIL") != NULL; }
int platform_can_open(void) { return 1; }
void platform_insets(int *top, int *bottom) { *top = *bottom = 0; }
void platform_take_photo(void) { fprintf(stderr, "No hay cámara en la versión de escritorio.\n"); }
const char *platform_mdc_path(const Options *o) { return o->output ? o->output : "salida.mdc"; }

/* El MdC guardado: se muestra en el Finder. */
void platform_share(const char *path, const char *text)
{
    (void)text;
    @autoreleasepool {
        NSString *p = [NSString stringWithUTF8String:path];
        if (![p isAbsolutePath])
            p = [[[NSFileManager defaultManager] currentDirectoryPath] stringByAppendingPathComponent:p];
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:p] ]];
    }
}

/* «Abrir»: el archivo elegido llega como si se hubiera soltado en la ventana. */
void platform_pick_file(void)
{
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.message = @"Elige una imagen o un PDF con jeroglíficos";
        if (@available(macOS 11.0, *))
            panel.allowedContentTypes = @[ UTTypeImage, UTTypePDF ];
        NSWindow *key = [NSApp keyWindow];
        if ([panel runModal] == NSModalResponseOK && panel.URL) {
            SDL_Event e;
            SDL_zero(e);
            e.type = SDL_DROPFILE;
            e.drop.file = SDL_strdup(panel.URL.fileSystemRepresentation);
            e.drop.timestamp = SDL_GetTicks();
            if (SDL_PushEvent(&e) != 1)
                SDL_free(e.drop.file);
        }
        [key makeKeyAndOrderFront:nil];
    }
}

#else
/* =====================================================================
 * iOS
 * ===================================================================== */
#import <UIKit/UIKit.h>
#import <PhotosUI/PhotosUI.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#define MAX_SIDE 3600   /* píxeles: como en Android */

static volatile Uint32 ev_type = 0;   /* 0 hasta que la interfaz esté lista */
static char mdc_path[1024] = "salida.mdc";

static void push_request(int kind, NSArray<NSString *> *files, NSString *msg)
{
    if (ev_type == 0)
        return;
    Request *r = calloc(1, sizeof *r);
    if (!r)
        return;
    r->kind = kind;
    if (msg)
        snprintf(r->msg, sizeof r->msg, "%s", msg.UTF8String);
    if (files.count) {
        r->files = calloc(files.count, sizeof(char *));
        for (NSString *f in files)
            r->files[r->n++] = strdup(f.fileSystemRepresentation);
    }
    SDL_Event e;
    SDL_zero(e);
    e.type = ev_type;
    e.user.data1 = r;
    if (SDL_PushEvent(&e) != 1)
        request_free(r);
}

static void status(NSString *msg) { push_request(REQ_STATUS, nil, msg); }

static UIViewController *top_controller(void)
{
    UIWindow *win = nil;
    for (UIScene *sc in UIApplication.sharedApplication.connectedScenes)
        if ([sc isKindOfClass:[UIWindowScene class]])
            for (UIWindow *w in ((UIWindowScene *)sc).windows)
                if (w.isKeyWindow || !win)
                    win = w;
    if (!win)
        win = UIApplication.sharedApplication.windows.firstObject;
    UIViewController *vc = win.rootViewController;
    while (vc.presentedViewController)
        vc = vc.presentedViewController;
    return vc;
}

/* En el iPad las hojas de acciones y de compartir salen de un punto de la pantalla. */
static void anchor(UIViewController *c, UIViewController *over)
{
    UIPopoverPresentationController *pop = c.popoverPresentationController;
    if (pop) {
        pop.sourceView = over.view;
        CGRect b = over.view.bounds;
        pop.sourceRect = CGRectMake(CGRectGetMidX(b), b.origin.y + 60, 1, 1);
        pop.permittedArrowDirections = UIPopoverArrowDirectionUp;
    }
}

static NSString *cache_dir(NSString *prefix)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *caches = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) firstObject];
    /* las anteriores se borran (la interfaz ya ha leído sus imágenes) */
    for (NSString *n in [fm contentsOfDirectoryAtPath:caches error:nil])
        if ([n hasPrefix:[prefix stringByAppendingString:@"-"]])
            [fm removeItemAtPath:[caches stringByAppendingPathComponent:n] error:nil];
    NSString *d = [caches stringByAppendingPathComponent:
                              [NSString stringWithFormat:@"%@-%.0f", prefix, NSDate.date.timeIntervalSince1970 * 1000]];
    [fm createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
    return d;
}

/* Una foto o imagen: enderezada (orientación EXIF), reducida y en PNG. (Hilo de trabajo.) */
static void import_uiimage(UIImage *img, BOOL photo)
{
    if (!img || img.size.width < 1 || img.size.height < 1) {
        status(@"El archivo no es una imagen que se pueda leer.");
        return;
    }
    @autoreleasepool {
        CGSize sz = img.size;   /* ya en la orientación buena */
        CGFloat big = sz.width > sz.height ? sz.width : sz.height;
        CGFloat k = big * img.scale > MAX_SIDE ? MAX_SIDE / big : img.scale;
        CGSize out = CGSizeMake(floor(sz.width * k), floor(sz.height * k));
        UIGraphicsImageRendererFormat *f = [UIGraphicsImageRendererFormat preferredFormat];
        f.scale = 1;
        f.opaque = YES;
        UIGraphicsImageRenderer *r = [[UIGraphicsImageRenderer alloc] initWithSize:out format:f];
        NSData *png = [r PNGDataWithActions:^(UIGraphicsImageRendererContext *c) {
            [UIColor.whiteColor setFill];
            [c fillRect:CGRectMake(0, 0, out.width, out.height)];
            [img drawInRect:CGRectMake(0, 0, out.width, out.height)];
        }];
        NSString *path = [cache_dir(@"imagen") stringByAppendingPathComponent:@"imagen.png"];
        if (![png writeToFile:path atomically:YES]) {
            status(@"No se puede guardar la imagen.");
            return;
        }
        push_request(photo ? REQ_PHOTO : REQ_DOCUMENT, @[ path ], nil);
    }
}

/* "71", "71-92", "71-", "-10", "3,5,7-9" -> rangos, como en escritorio */
static void render_pdf(NSString *pdf, NSString *spec, int total)
{
    int ranges[64][2];
    int nr = parse_page_ranges(spec.UTF8String, ranges, 64);
    if (nr == 0) {
        status([NSString stringWithFormat:@"Rango de páginas no válido: %@", spec]);
        return;
    }
    NSString *dir = cache_dir(@"pdf");
    status(@"Rasterizando el PDF...");
    if (apple_pdf_render(pdf.fileSystemRepresentation, 300, ranges, nr, dir.fileSystemRepresentation) != 0) {
        status([NSString stringWithFormat:@"Ninguna de esas páginas está en el PDF (tiene %d).", total]);
        return;
    }
    NSArray *names = [[[NSFileManager.defaultManager contentsOfDirectoryAtPath:dir error:nil]
        filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"self ENDSWITH '.png'"]]
        sortedArrayUsingSelector:@selector(compare:)];
    NSMutableArray *files = [NSMutableArray array];
    for (NSString *n in names)
        [files addObject:[dir stringByAppendingPathComponent:n]];
    push_request(REQ_DOCUMENT, files, nil);
}

static void ask_pages(NSString *pdf)
{
    CGPDFDocumentRef doc = open_pdf(pdf.fileSystemRepresentation);
    if (!doc) {
        status(@"No se puede abrir el PDF.");
        return;
    }
    int total = (int)CGPDFDocumentGetNumberOfPages(doc);
    CGPDFDocumentRelease(doc);
    if (total <= 1) {
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ render_pdf(pdf, @"1", total); });
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *a = [UIAlertController
            alertControllerWithTitle:[NSString stringWithFormat:@"PDF de %d páginas", total]
                             message:@"¿Qué páginas quieres reconocer? Por ejemplo: 7, 71-92, 71- o 3,5,7-9"
                      preferredStyle:UIAlertControllerStyleAlert];
        [a addTextFieldWithConfigurationHandler:^(UITextField *t) {
            t.text = total <= 5 ? [NSString stringWithFormat:@"1-%d", total] : @"1";
            t.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
            t.clearButtonMode = UITextFieldViewModeWhileEditing;
        }];
        __weak UIAlertController *wa = a;
        [a addAction:[UIAlertAction actionWithTitle:@"Cancelar" style:UIAlertActionStyleCancel
                                            handler:^(UIAlertAction *x) { (void)x; status(@"Cancelado."); }]];
        [a addAction:[UIAlertAction actionWithTitle:@"Aceptar" style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *x) {
            (void)x;
            NSString *spec = wa.textFields.firstObject.text ?: @"1";
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ render_pdf(pdf, spec, total); });
        }]];
        [top_controller() presentViewController:a animated:YES completion:nil];
    });
}

/* Un archivo elegido (o abierto desde otra app): PDF, modelo .hmod o imagen. (Hilo de trabajo.) */
static void import_file(NSURL *url)
{
    BOOL scoped = [url startAccessingSecurityScopedResource];
    NSString *name = url.lastPathComponent.lowercaseString;
    status([NSString stringWithFormat:@"Leyendo %@...", url.lastPathComponent]);
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *dir = [[NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) firstObject]
        stringByAppendingPathComponent:@"entrada"];
    [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    if ([name hasSuffix:@".hmod"]) {
        NSString *dst = @"datos/modelo_fotos.hmod";   /* (la carpeta actual: Application Support) */
        [fm removeItemAtPath:dst error:nil];
        BOOL ok = [fm copyItemAtPath:url.path toPath:dst error:nil];
        if (scoped)
            [url stopAccessingSecurityScopedResource];
        if (ok)
            push_request(REQ_MODEL, nil, nil);
        else
            status(@"No se puede copiar el modelo.");
        return;
    }
    BOOL pdf = [name hasSuffix:@".pdf"];
    NSString *dst = [dir stringByAppendingPathComponent:pdf ? @"documento.pdf" : @"imagen"];
    [fm removeItemAtPath:dst error:nil];
    NSError *err = nil;
    BOOL ok = [fm copyItemAtURL:url toURL:[NSURL fileURLWithPath:dst] error:&err];
    if (scoped)
        [url stopAccessingSecurityScopedResource];
    if (!ok) {
        status([NSString stringWithFormat:@"No se puede abrir el archivo: %@", err.localizedDescription]);
        return;
    }
    if (pdf)
        ask_pages(dst);
    else
        import_uiimage([UIImage imageWithContentsOfFile:dst], YES);
}

/* ---------- selectores ---------- */

@interface HOPicker : NSObject <UIDocumentPickerDelegate, PHPickerViewControllerDelegate,
                                UIImagePickerControllerDelegate, UINavigationControllerDelegate>
@end

@implementation HOPicker

- (void)documentPicker:(UIDocumentPickerViewController *)c didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    (void)c;
    NSURL *u = urls.firstObject;
    if (u)
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ import_file(u); });
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)c { (void)c; }

- (void)picker:(PHPickerViewController *)p didFinishPicking:(NSArray<PHPickerResult *> *)results
{
    [p dismissViewControllerAnimated:YES completion:nil];
    NSItemProvider *ip = results.firstObject.itemProvider;
    if (!ip)
        return;
    if (![ip canLoadObjectOfClass:[UIImage class]]) {
        status(@"No se puede leer esa imagen.");
        return;
    }
    status(@"Leyendo la imagen...");
    [ip loadObjectOfClass:[UIImage class] completionHandler:^(id<NSItemProviderReading> obj, NSError *err) {
        (void)err;
        import_uiimage((UIImage *)obj, YES);
    }];
}

- (void)imagePickerController:(UIImagePickerController *)p didFinishPickingMediaWithInfo:(NSDictionary *)info
{
    UIImage *img = info[UIImagePickerControllerOriginalImage];
    [p dismissViewControllerAnimated:YES completion:nil];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ import_uiimage(img, YES); });
}

- (void)imagePickerControllerDidCancel:(UIImagePickerController *)p
{
    [p dismissViewControllerAnimated:YES completion:nil];
}

@end

static HOPicker *picker;

static void open_files(void)
{
    UIDocumentPickerViewController *d = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[ UTTypeImage, UTTypePDF, UTTypeData ] asCopy:YES];
    d.delegate = picker;
    d.allowsMultipleSelection = NO;
    [top_controller() presentViewController:d animated:YES completion:nil];
}

static void open_photos(void)
{
    PHPickerConfiguration *cfg = [[PHPickerConfiguration alloc] init];
    cfg.filter = [PHPickerFilter imagesFilter];
    cfg.selectionLimit = 1;
    PHPickerViewController *p = [[PHPickerViewController alloc] initWithConfiguration:cfg];
    p.delegate = picker;
    [top_controller() presentViewController:p animated:YES completion:nil];
}

int platform_is_mobile(void) { return 1; }
int platform_can_open(void) { return 1; }
const char *platform_mdc_path(const Options *o) { (void)o; return mdc_path; }

void platform_insets(int *top, int *bottom)
{
    UIView *v = top_controller().view;
    UIEdgeInsets in = v ? v.window.safeAreaInsets : UIEdgeInsetsZero;
    *top = (int)in.top;
    *bottom = (int)in.bottom;
}

void platform_pick_file(void)
{
    UIViewController *vc = top_controller();
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Abrir"
                                                                message:@"Una imagen o un PDF con jeroglíficos"
                                                         preferredStyle:UIAlertControllerStyleActionSheet];
    [a addAction:[UIAlertAction actionWithTitle:@"Fototeca" style:UIAlertActionStyleDefault
                                        handler:^(UIAlertAction *x) { (void)x; open_photos(); }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Archivos (imagen, PDF o modelo .hmod)"
                                          style:UIAlertActionStyleDefault
                                        handler:^(UIAlertAction *x) { (void)x; open_files(); }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Cancelar" style:UIAlertActionStyleCancel handler:nil]];
    anchor(a, vc);
    [vc presentViewController:a animated:YES completion:nil];
}

void platform_take_photo(void)
{
    if (![UIImagePickerController isSourceTypeAvailable:UIImagePickerControllerSourceTypeCamera]) {
        status(@"Este dispositivo no tiene cámara.");
        return;
    }
    UIImagePickerController *p = [[UIImagePickerController alloc] init];
    p.sourceType = UIImagePickerControllerSourceTypeCamera;
    p.delegate = picker;
    [top_controller() presentViewController:p animated:YES completion:nil];
}

void platform_share(const char *path, const char *text)
{
    UIViewController *vc = top_controller();
    NSMutableArray *items = [NSMutableArray array];
    NSURL *u = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    if (u)
        [items addObject:u];
    else if (text && *text)
        [items addObject:[NSString stringWithUTF8String:text]];
    UIActivityViewController *s = [[UIActivityViewController alloc] initWithActivityItems:items applicationActivities:nil];
    anchor(s, vc);
    [vc presentViewController:s animated:YES completion:nil];
}

void apple_startup(void) {}

/* Los archivos que otras apps mandan a HieroOCR («Abrir en...») llegan a SDL
 * como SDL_DROPFILE: se importan como los elegidos aquí. */
static int SDLCALL drop_filter(void *user, SDL_Event *e)
{
    (void)user;
    if (e->type != SDL_DROPFILE)
        return 1;
    if (e->drop.file) {
        NSURL *u = [NSURL fileURLWithPath:[NSString stringWithUTF8String:e->drop.file]];
        SDL_free(e->drop.file);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ import_file(u); });
    }
    return 0;
}

/* Los datos del paquete (modelo, lista de signos, diccionario, fuentes) se
 * copian a Application Support, donde también se guarda un modelo elegido. */
static void install_data(NSString *home)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *src = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"datos"];
    NSString *dst = [home stringByAppendingPathComponent:@"datos"];
    NSString *stamp = [dst stringByAppendingPathComponent:@".version"];
    NSDictionary *info = NSBundle.mainBundle.infoDictionary;
    NSString *version = [NSString stringWithFormat:@"%@/%@", info[@"CFBundleShortVersionString"], info[@"CFBundleVersion"]];
    NSDictionary *attrs = [fm attributesOfItemAtPath:NSBundle.mainBundle.executablePath error:nil];
    version = [version stringByAppendingFormat:@"/%.0f", [attrs.fileModificationDate timeIntervalSince1970]];
    if ([[NSString stringWithContentsOfFile:stamp encoding:NSUTF8StringEncoding error:nil] isEqualToString:version])
        return;
    [fm createDirectoryAtPath:dst withIntermediateDirectories:YES attributes:nil error:nil];
    for (NSString *n in [fm contentsOfDirectoryAtPath:src error:nil]) {
        NSString *to = [dst stringByAppendingPathComponent:n];
        [fm removeItemAtPath:to error:nil];
        [fm copyItemAtPath:[src stringByAppendingPathComponent:n] toPath:to error:nil];
    }
    [version writeToFile:stamp atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *home = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES) firstObject];
    NSString *docs = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    [fm createDirectoryAtPath:home withIntermediateDirectories:YES attributes:nil error:nil];
    [fm createDirectoryAtPath:docs withIntermediateDirectories:YES attributes:nil error:nil];
    install_data(home);
    if (chdir(home.fileSystemRepresentation) != 0)
        NSLog(@"hieroocr: chdir(%@) ha fallado", home);
    snprintf(mdc_path, sizeof mdc_path, "%s/salida.mdc", docs.fileSystemRepresentation);
    /* stderr a registro.txt (en Documentos, para poder verlo en Archivos) */
    NSString *log = [docs stringByAppendingPathComponent:@"registro.txt"];
    if (freopen(log.fileSystemRepresentation, "w", stderr))
        setvbuf(stderr, NULL, _IONBF, 0);
    picker = [[HOPicker alloc] init];

    Options o = {
        .dpi = 300, .uncertain_thr = 0.11f, .tmpl_dir = "plantillas", .gui = 1,
    };
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        NSLog(@"hieroocr: SDL_Init: %s", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    if (TTF_Init() != 0) {
        NSLog(@"hieroocr: TTF_Init: %s", TTF_GetError());
        return 1;
    }
    ev_type = gui_event_type();
    SDL_SetEventFilter(drop_filter, NULL);

    app_find_data(&o);
    classify_set_options(&o);
    TemplateSet ts;
    app_load_templates(&o, &ts, 0);
    fprintf(stderr, "Modelo %s: %d plantillas\n", o.model ? o.model : "(ninguno)", ts.n);

    /* Pruebas en el simulador: HIEROOCR_ABRIR=ruta abre un archivo al arrancar. */
    const char *test = getenv("HIEROOCR_ABRIR");
    if (test && *test) {
        NSURL *u = [NSURL fileURLWithPath:[NSString stringWithUTF8String:test]];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC),
                       dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ import_file(u); });
    }

    Doc doc = {0};
    int rc = gui_run(&doc, &o, &ts);
    doc_free(&doc);
    ts_free(&ts);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return rc;
}

#endif /* HIERO_IOS */
#endif /* __APPLE__ */
