/**
 * @file make_icon.mm
 * @brief Renders the application icon (an .iconset of PNGs) for the macOS bundle.
 *
 * Usage: make_icon <output.iconset>   then: iconutil -c icns <output.iconset>
 * The mark: two overlapping rounded squares (physical and digital twin) and a
 * check seal (verified), on the product's steel-blue gradient.
 */
#import <Cocoa/Cocoa.h>

static void Draw(CGFloat s) {
    const NSRect r = NSMakeRect(0, 0, s, s);
    const CGFloat inset = s * 0.09;
    NSBezierPath* tile = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, inset, inset)
                                                         xRadius:s * 0.19
                                                         yRadius:s * 0.19];
    NSGradient* g = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithSRGBRed:0.16 green:0.40 blue:0.72 alpha:1]
                                                  endingColor:[NSColor colorWithSRGBRed:0.07 green:0.20 blue:0.40 alpha:1]];
    [g drawInBezierPath:tile angle:-90];
    // Physical twin (outline) and digital twin (filled), offset.
    const CGFloat w = s * 0.36;
    NSRect a = NSMakeRect(s * 0.22, s * 0.34, w, w);
    NSRect b = NSMakeRect(s * 0.40, s * 0.22, w, w);
    NSBezierPath* pa = [NSBezierPath bezierPathWithRoundedRect:a xRadius:s * 0.06 yRadius:s * 0.06];
    pa.lineWidth = s * 0.035;
    [[NSColor colorWithWhite:1 alpha:0.75] setStroke];
    [pa stroke];
    NSBezierPath* pb = [NSBezierPath bezierPathWithRoundedRect:b xRadius:s * 0.06 yRadius:s * 0.06];
    [[NSColor whiteColor] setFill];
    [pb fill];
    // Check mark inside the digital twin.
    NSBezierPath* check = [NSBezierPath bezierPath];
    [check moveToPoint:NSMakePoint(NSMinX(b) + w * 0.22, NSMinY(b) + w * 0.50)];
    [check lineToPoint:NSMakePoint(NSMinX(b) + w * 0.43, NSMinY(b) + w * 0.29)];
    [check lineToPoint:NSMakePoint(NSMinX(b) + w * 0.80, NSMinY(b) + w * 0.72)];
    check.lineWidth = s * 0.05;
    check.lineCapStyle = NSLineCapStyleRound;
    check.lineJoinStyle = NSLineJoinStyleRound;
    [[NSColor colorWithSRGBRed:0.11 green:0.31 blue:0.57 alpha:1] setStroke];
    [check stroke];
}

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        if (argc != 2) {
            fprintf(stderr, "usage: make_icon <output.iconset>\n");
            return 2;
        }
        NSString* dir = [NSString stringWithUTF8String:argv[1]];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        const struct { int points; int scale; } sizes[] = {{16, 1}, {16, 2}, {32, 1}, {32, 2}, {128, 1},
                                                           {128, 2}, {256, 1}, {256, 2}, {512, 1}, {512, 2}};
        for (const auto& sz : sizes) {
            const int px = sz.points * sz.scale;
            NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:px pixelsHigh:px
                                                                        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES
                                                                             isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
                                                                          bytesPerRow:0 bitsPerPixel:0];
            [NSGraphicsContext saveGraphicsState];
            [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:rep]];
            Draw(px);
            [NSGraphicsContext restoreGraphicsState];
            NSString* name = sz.scale == 1 ? [NSString stringWithFormat:@"icon_%dx%d.png", sz.points, sz.points]
                                           : [NSString stringWithFormat:@"icon_%dx%d@2x.png", sz.points, sz.points];
            NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            [png writeToFile:[dir stringByAppendingPathComponent:name] atomically:YES];
        }
    }
    return 0;
}
