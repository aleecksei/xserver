/*
 * This code was stolen from RAC and adapted to control the legacy vga
 * interface.
 *
 *
 * Copyright (c) 2007 Paulo R. Zanoni, Tiago Vignatti
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "xorg-config.h"

#include "xf86VGAarbiter.h"

#ifdef HAVE_PCI_DEVICE_VGAARB_INIT
#include "xf86VGAarbiterPriv.h"
#include "xf86Bus.h"
#include "xf86Priv.h"
#include "pciaccess.h"


static GCFuncs VGAarbiterGCFuncs = {
    VGAarbiterValidateGC, VGAarbiterChangeGC, VGAarbiterCopyGC,
    VGAarbiterDestroyGC, VGAarbiterChangeClip, VGAarbiterDestroyClip,
    VGAarbiterCopyClip
};

static GCOps VGAarbiterGCOps = {
    VGAarbiterFillSpans, VGAarbiterSetSpans, VGAarbiterPutImage,
    VGAarbiterCopyArea, VGAarbiterCopyPlane, VGAarbiterPolyPoint,
    VGAarbiterPolylines, VGAarbiterPolySegment, VGAarbiterPolyRectangle,
    VGAarbiterPolyArc, VGAarbiterFillPolygon, VGAarbiterPolyFillRect,
    VGAarbiterPolyFillArc, VGAarbiterPolyText8, VGAarbiterPolyText16,
    VGAarbiterImageText8, VGAarbiterImageText16, VGAarbiterImageGlyphBlt,
    VGAarbiterPolyGlyphBlt, VGAarbiterPushPixels,
    {NULL}      /* devPrivate */
};

static miPointerSpriteFuncRec VGAarbiterSpriteFuncs = {
    VGAarbiterSpriteRealizeCursor, VGAarbiterSpriteUnrealizeCursor,
    VGAarbiterSpriteSetCursor, VGAarbiterSpriteMoveCursor,
    VGAarbiterDeviceCursorInitialize, VGAarbiterDeviceCursorCleanup
};

static int VGAarbiterKeyIndex;
static DevPrivateKey VGAarbiterScreenKey = &VGAarbiterKeyIndex;
static int VGAarbiterGCIndex;
static DevPrivateKey VGAarbiterGCKey = &VGAarbiterGCIndex;

static int vga_no_arb = 0;
void
xf86VGAarbiterInit(void)
{
    if (pci_device_vgaarb_init() != 0) {
	vga_no_arb = 1;
        xf86Msg(X_WARNING, "VGA arbiter: cannot open kernel arbiter, no multi-card support\n");
    }
}

void
xf86VGAarbiterFini(void)
{
    if (vga_no_arb)
	return;
    pci_device_vgaarb_fini();
}

void
xf86VGAarbiterLock(ScrnInfoPtr pScrn)
{
    if (vga_no_arb)
	return;
    pci_device_vgaarb_set_target(pScrn->vgaDev);
    pci_device_vgaarb_lock();
}

void
xf86VGAarbiterUnlock(ScrnInfoPtr pScrn)
{
    if (vga_no_arb)
	return;
    pci_device_vgaarb_unlock();
}

Bool xf86VGAarbiterAllowDRI(ScreenPtr pScreen)
{
    int vga_count;
    int rsrc_decodes;
    ScrnInfoPtr         pScrn = xf86Screens[pScreen->myNum];

    if (vga_no_arb)
	return TRUE;

    pci_device_vgaarb_get_info(pScrn->vgaDev, &vga_count, &rsrc_decodes);
    if (vga_count > 1) {
        if (rsrc_decodes) {
            return FALSE;
        }
    }
    return TRUE;
}

void
xf86VGAarbiterScrnInit(ScrnInfoPtr pScrn)
{
    struct pci_device *dev;
    EntityPtr pEnt;

    if (vga_no_arb)
	return;

    pEnt = xf86Entities[pScrn->entityList[0]];
    if (pEnt->bus.type != BUS_PCI)
	return;

    dev = pEnt->bus.id.pci;
    pScrn->vgaDev = dev;
}

void
xf86VGAarbiterDeviceDecodes(ScrnInfoPtr pScrn)
{
    if (vga_no_arb)
	return;
    pci_device_vgaarb_decodes(VGA_ARB_RSRC_LEGACY_MEM | VGA_ARB_RSRC_LEGACY_IO);
}

Bool
xf86VGAarbiterWrapFunctions(void)
{
    ScrnInfoPtr pScrn;
    VGAarbiterScreenPtr pScreenPriv;
    miPointerScreenPtr PointPriv;
#ifdef RENDER
    PictureScreenPtr    ps;
#endif
    ScreenPtr pScreen;
    int vga_count, i;

    if (vga_no_arb)
        return FALSE;

    /*
     * we need to wrap the arbiter if we have more than
     * one VGA card - hotplug cries.
     */
    pci_device_vgaarb_get_info(NULL, &vga_count, NULL);
    if (vga_count < 2 || !xf86Screens)
        return FALSE;

    xf86Msg(X_INFO,"Found %d VGA devices: arbiter wrapping enabled\n",
            vga_count);

    for (i = 0; i < xf86NumScreens; i++) {
        pScreen = xf86Screens[i]->pScreen;
#ifdef RENDER
        ps = GetPictureScreenIfSet(pScreen);
#endif
        pScrn = xf86Screens[pScreen->myNum];
        PointPriv = dixLookupPrivate(&pScreen->devPrivates, miPointerScreenKey);

        if (!dixRequestPrivate(VGAarbiterGCKey, sizeof(VGAarbiterGCRec)))
            return FALSE;

        if (!(pScreenPriv = xalloc(sizeof(VGAarbiterScreenRec))))
            return FALSE;

        dixSetPrivate(&pScreen->devPrivates, VGAarbiterScreenKey, pScreenPriv);

        WRAP_SCREEN(CloseScreen, VGAarbiterCloseScreen);
        WRAP_SCREEN(SaveScreen, VGAarbiterSaveScreen);
        WRAP_SCREEN(WakeupHandler, VGAarbiterWakeupHandler);
        WRAP_SCREEN(BlockHandler, VGAarbiterBlockHandler);
        WRAP_SCREEN(CreateGC, VGAarbiterCreateGC);
        WRAP_SCREEN(GetImage, VGAarbiterGetImage);
        WRAP_SCREEN(GetSpans, VGAarbiterGetSpans);
        WRAP_SCREEN(SourceValidate, VGAarbiterSourceValidate);
        WRAP_SCREEN(CopyWindow, VGAarbiterCopyWindow);
        WRAP_SCREEN(ClearToBackground, VGAarbiterClearToBackground);
        WRAP_SCREEN(CreatePixmap, VGAarbiterCreatePixmap);
        WRAP_SCREEN(StoreColors, VGAarbiterStoreColors);
        WRAP_SCREEN(DisplayCursor, VGAarbiterDisplayCursor);
        WRAP_SCREEN(RealizeCursor, VGAarbiterRealizeCursor);
        WRAP_SCREEN(UnrealizeCursor, VGAarbiterUnrealizeCursor);
        WRAP_SCREEN(RecolorCursor, VGAarbiterRecolorCursor);
        WRAP_SCREEN(SetCursorPosition, VGAarbiterSetCursorPosition);
#ifdef RENDER
        WRAP_PICT(Composite,VGAarbiterComposite);
        WRAP_PICT(Glyphs,VGAarbiterGlyphs);
        WRAP_PICT(CompositeRects,VGAarbiterCompositeRects);
#endif
        WRAP_SCREEN_INFO(AdjustFrame, VGAarbiterAdjustFrame);
        WRAP_SCREEN_INFO(SwitchMode, VGAarbiterSwitchMode);
        WRAP_SCREEN_INFO(EnterVT, VGAarbiterEnterVT);
        WRAP_SCREEN_INFO(LeaveVT, VGAarbiterLeaveVT);
        WRAP_SCREEN_INFO(FreeScreen, VGAarbiterFreeScreen);
        WRAP_SPRITE;
    }

    return TRUE;
}

/* Screen funcs */
static Bool
VGAarbiterCloseScreen (int i, ScreenPtr pScreen)
{
    Bool val;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VGAarbiterScreenPtr pScreenPriv = (VGAarbiterScreenPtr)dixLookupPrivate(
        &pScreen->devPrivates, VGAarbiterScreenKey);
    miPointerScreenPtr PointPriv = (miPointerScreenPtr)dixLookupPrivate(
        &pScreen->devPrivates, miPointerScreenKey);
#ifdef RENDER
    PictureScreenPtr    ps = GetPictureScreenIfSet(pScreen);
#endif

    UNWRAP_SCREEN(CreateGC);
    UNWRAP_SCREEN(CloseScreen);
    UNWRAP_SCREEN(GetImage);
    UNWRAP_SCREEN(GetSpans);
    UNWRAP_SCREEN(SourceValidate);
    UNWRAP_SCREEN(CopyWindow);
    UNWRAP_SCREEN(ClearToBackground);
    UNWRAP_SCREEN(SaveScreen);
    UNWRAP_SCREEN(StoreColors);
    UNWRAP_SCREEN(DisplayCursor);
    UNWRAP_SCREEN(RealizeCursor);
    UNWRAP_SCREEN(UnrealizeCursor);
    UNWRAP_SCREEN(RecolorCursor);
    UNWRAP_SCREEN(SetCursorPosition);
#ifdef RENDER
    UNWRAP_PICT(Composite);
    UNWRAP_PICT(Glyphs);
    UNWRAP_PICT(CompositeRects);
#endif
    UNWRAP_SCREEN_INFO(AdjustFrame);
    UNWRAP_SCREEN_INFO(SwitchMode);
    UNWRAP_SCREEN_INFO(EnterVT);
    UNWRAP_SCREEN_INFO(LeaveVT);
    UNWRAP_SCREEN_INFO(FreeScreen);
    UNWRAP_SPRITE;

    xfree ((pointer) pScreenPriv);
    xf86VGAarbiterLock(xf86Screens[i]);
    val = (*pScreen->CloseScreen) (i, pScreen);
    xf86VGAarbiterUnlock(xf86Screens[i]);
    return val;
}

static void
VGAarbiterBlockHandler(int i,
                       pointer blockData, pointer pTimeout, pointer pReadmask)
{
    ScreenPtr pScreen = screenInfo.screens[i];
    SCREEN_PROLOG(BlockHandler);
    VGAGet();
    pScreen->BlockHandler(i, blockData, pTimeout, pReadmask);
    VGAPut();
    SCREEN_EPILOG(BlockHandler, VGAarbiterBlockHandler);
}

static void
VGAarbiterWakeupHandler(int i, pointer blockData, unsigned long result, pointer pReadmask)
{
    ScreenPtr pScreen = screenInfo.screens[i];
    SCREEN_PROLOG(WakeupHandler);
    VGAGet();
    pScreen->WakeupHandler(i, blockData, result, pReadmask);
    VGAPut();
    SCREEN_EPILOG(WakeupHandler, VGAarbiterWakeupHandler);
}

static void
VGAarbiterGetImage (
    DrawablePtr pDrawable,
    int sx, int sy, int w, int h,
    unsigned int    format,
    unsigned long   planemask,
    char        *pdstLine
    )
{
    ScreenPtr pScreen = pDrawable->pScreen;
    SCREEN_PROLOG(GetImage);
//    if (xf86Screens[pScreen->myNum]->vtSema) {
    VGAGet();
//    }
    (*pScreen->GetImage) (pDrawable, sx, sy, w, h,
              format, planemask, pdstLine);
    VGAPut();
    SCREEN_EPILOG (GetImage, VGAarbiterGetImage);
}

static void
VGAarbiterGetSpans (
    DrawablePtr pDrawable,
    int     wMax,
    DDXPointPtr ppt,
    int     *pwidth,
    int     nspans,
    char    *pdstStart
    )
{
    ScreenPtr       pScreen = pDrawable->pScreen;

    SCREEN_PROLOG (GetSpans);
    VGAGet();
    (*pScreen->GetSpans) (pDrawable, wMax, ppt, pwidth, nspans, pdstStart);
    VGAPut();
    SCREEN_EPILOG (GetSpans, VGAarbiterGetSpans);
}

static void
VGAarbiterSourceValidate (
    DrawablePtr pDrawable,
    int x, int y, int width, int height )
{
    ScreenPtr   pScreen = pDrawable->pScreen;
    SCREEN_PROLOG (SourceValidate);
    VGAGet();
    if (pScreen->SourceValidate)
    (*pScreen->SourceValidate) (pDrawable, x, y, width, height);
    VGAPut();
    SCREEN_EPILOG (SourceValidate, VGAarbiterSourceValidate);
}

static void
VGAarbiterCopyWindow(
    WindowPtr pWin,
    DDXPointRec ptOldOrg,
    RegionPtr prgnSrc )
{
    ScreenPtr pScreen = pWin->drawable.pScreen;

    SCREEN_PROLOG (CopyWindow);
    VGAGet();
    (*pScreen->CopyWindow) (pWin, ptOldOrg, prgnSrc);
    VGAPut();
    SCREEN_EPILOG (CopyWindow, VGAarbiterCopyWindow);
}

static void
VGAarbiterClearToBackground (
    WindowPtr pWin,
    int x, int y,
    int w, int h,
    Bool generateExposures )
{
    ScreenPtr pScreen = pWin->drawable.pScreen;

    SCREEN_PROLOG ( ClearToBackground);
    VGAGet();
    (*pScreen->ClearToBackground) (pWin, x, y, w, h, generateExposures);
    VGAPut();
    SCREEN_EPILOG (ClearToBackground, VGAarbiterClearToBackground);
}

static PixmapPtr
VGAarbiterCreatePixmap(ScreenPtr pScreen, int w, int h, int depth, unsigned usage_hint)
{
    PixmapPtr pPix;

    SCREEN_PROLOG ( CreatePixmap);
    VGAGet();
    pPix = (*pScreen->CreatePixmap) (pScreen, w, h, depth, usage_hint);
    VGAPut();
    SCREEN_EPILOG (CreatePixmap, VGAarbiterCreatePixmap);

    return pPix;
}

static Bool
VGAarbiterSaveScreen(ScreenPtr pScreen, Bool unblank)
{
    Bool val;

    SCREEN_PROLOG (SaveScreen);
    VGAGet();
    val = (*pScreen->SaveScreen) (pScreen, unblank);
    VGAPut();
    SCREEN_EPILOG (SaveScreen, VGAarbiterSaveScreen);

    return val;
}

static void
VGAarbiterStoreColors (
    ColormapPtr        pmap,
    int                ndef,
    xColorItem         *pdefs)
{
    ScreenPtr pScreen = pmap->pScreen;

    SCREEN_PROLOG (StoreColors);
    VGAGet();
    (*pScreen->StoreColors) (pmap,ndef,pdefs);
    VGAPut();
    SCREEN_EPILOG ( StoreColors, VGAarbiterStoreColors);
}

static void
VGAarbiterRecolorCursor (
    DeviceIntPtr pDev,
    ScreenPtr pScreen,
    CursorPtr pCurs,
    Bool displayed
    )
{
    SCREEN_PROLOG (RecolorCursor);
    VGAGet();
    (*pScreen->RecolorCursor) (pDev, pScreen, pCurs, displayed);
    VGAPut();
    SCREEN_EPILOG ( RecolorCursor, VGAarbiterRecolorCursor);
}

static Bool
VGAarbiterRealizeCursor (
    DeviceIntPtr pDev,
    ScreenPtr   pScreen,
    CursorPtr   pCursor
    )
{
    Bool val;

    SCREEN_PROLOG (RealizeCursor);
    VGAGet();
    val = (*pScreen->RealizeCursor) (pDev, pScreen,pCursor);
    VGAPut();
    SCREEN_EPILOG ( RealizeCursor, VGAarbiterRealizeCursor);
    return val;
}

static Bool
VGAarbiterUnrealizeCursor (
    DeviceIntPtr pDev,
    ScreenPtr   pScreen,
    CursorPtr   pCursor
    )
{
    Bool val;

    SCREEN_PROLOG (UnrealizeCursor);
    VGAGet();
    val = (*pScreen->UnrealizeCursor) (pDev, pScreen, pCursor);
    VGAPut();
    SCREEN_EPILOG ( UnrealizeCursor, VGAarbiterUnrealizeCursor);
    return val;
}

static Bool
VGAarbiterDisplayCursor (
    DeviceIntPtr pDev,
    ScreenPtr   pScreen,
    CursorPtr   pCursor
    )
{
    Bool val;

    SCREEN_PROLOG (DisplayCursor);
    VGAGet();
    val = (*pScreen->DisplayCursor) (pDev, pScreen, pCursor);
    VGAPut();
    SCREEN_EPILOG ( DisplayCursor, VGAarbiterDisplayCursor);
    return val;
}

static Bool
VGAarbiterSetCursorPosition (
    DeviceIntPtr pDev,
    ScreenPtr   pScreen,
    int x, int y,
    Bool generateEvent)
{
    Bool val;

    SCREEN_PROLOG (SetCursorPosition);
    VGAGet();
    val = (*pScreen->SetCursorPosition) (pDev, pScreen, x, y, generateEvent);
    VGAPut();
    SCREEN_EPILOG ( SetCursorPosition, VGAarbiterSetCursorPosition);
    return val;
}

static void
VGAarbiterAdjustFrame(int index, int x, int y, int flags)
{
    ScreenPtr pScreen = screenInfo.screens[index];
    VGAarbiterScreenPtr pScreenPriv = (VGAarbiterScreenPtr)dixLookupPrivate(
        &pScreen->devPrivates, VGAarbiterScreenKey);

    VGAGet();
    (*pScreenPriv->AdjustFrame)(index, x, y, flags);
    VGAPut();
}

static Bool
VGAarbiterSwitchMode(int index, DisplayModePtr mode, int flags)
{
    Bool val;
    ScreenPtr pScreen = screenInfo.screens[index];
    VGAarbiterScreenPtr pScreenPriv = (VGAarbiterScreenPtr)dixLookupPrivate(
        &pScreen->devPrivates, VGAarbiterScreenKey);

    VGAGet();
    val = (*pScreenPriv->SwitchMode)(index, mode, flags);
    VGAPut();
    return val;
}

static Bool
VGAarbiterEnterVT(int index, int flags)
{
    Bool val;
    ScrnInfoPtr pScrn = xf86Screens[index];
    ScreenPtr pScreen = screenInfo.screens[index];
    VGAarbiterScreenPtr pScreenPriv = (VGAarbiterScreenPtr)dixLookupPrivate(
        &pScreen->devPrivates, VGAarbiterScreenKey);

    VGAGet();
    pScrn->EnterVT = pScreenPriv->EnterVT;
    val = (*pScrn->EnterVT)(index, flags);
    pScreenPriv->EnterVT = pScrn->EnterVT;
    pScrn->EnterVT = VGAarbiterEnterVT;
    VGAPut();
    return val;
}

static void
VGAarbiterLeaveVT(int index, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[index];
    ScreenPtr pScreen = screenInfo.screens[index];
    VGAarbiterScreenPtr pScreenPriv = (VGAarbiterScreenPtr)dixLookupPrivate(
        &pScreen->devPrivates, VGAarbiterScreenKey);

    VGAGet();
    pScrn->LeaveVT = pScreenPriv->LeaveVT;
    (*pScreenPriv->LeaveVT)(index, flags);
    pScreenPriv->LeaveVT = pScrn->LeaveVT;
    pScrn->LeaveVT = VGAarbiterLeaveVT;
    VGAPut();
}

static void
VGAarbiterFreeScreen(int index, int flags)
{
    ScreenPtr pScreen = screenInfo.screens[index];
    VGAarbiterScreenPtr pScreenPriv = (VGAarbiterScreenPtr)dixLookupPrivate(
        &pScreen->devPrivates, VGAarbiterScreenKey);

    VGAGet();
    (*pScreenPriv->FreeScreen)(index, flags);
    VGAPut();
}

static Bool
VGAarbiterCreateGC(GCPtr pGC)
{
    ScreenPtr    pScreen = pGC->pScreen;
    VGAarbiterGCPtr pGCPriv = (VGAarbiterGCPtr)dixLookupPrivate(&pGC->devPrivates, VGAarbiterGCKey);
    Bool         ret;

    SCREEN_PROLOG(CreateGC);
    VGAGet();
    ret = (*pScreen->CreateGC)(pGC);
    VGAPut();
    GC_WRAP(pGC);
    SCREEN_EPILOG(CreateGC,VGAarbiterCreateGC);

    return ret;
}

/* GC funcs */
static void
VGAarbiterValidateGC(
   GCPtr         pGC,
   unsigned long changes,
   DrawablePtr   pDraw )
{
    GC_UNWRAP(pGC);
    (*pGC->funcs->ValidateGC)(pGC, changes, pDraw);
    GC_WRAP(pGC);
}


static void
VGAarbiterDestroyGC(GCPtr pGC)
{
    GC_UNWRAP (pGC);
    (*pGC->funcs->DestroyGC)(pGC);
    GC_WRAP (pGC);
}

static void
VGAarbiterChangeGC (
    GCPtr       pGC,
    unsigned long   mask)
{
    GC_UNWRAP (pGC);
    (*pGC->funcs->ChangeGC) (pGC, mask);
    GC_WRAP (pGC);
}

static void
VGAarbiterCopyGC (
    GCPtr       pGCSrc,
    unsigned long   mask,
    GCPtr       pGCDst)
{
    GC_UNWRAP (pGCDst);
    (*pGCDst->funcs->CopyGC) (pGCSrc, mask, pGCDst);
    GC_WRAP (pGCDst);
}

static void
VGAarbiterChangeClip (
    GCPtr   pGC,
    int     type,
    pointer pvalue,
    int     nrects )
{
    GC_UNWRAP (pGC);
    (*pGC->funcs->ChangeClip) (pGC, type, pvalue, nrects);
    GC_WRAP (pGC);
}

static void
VGAarbiterCopyClip(GCPtr pgcDst, GCPtr pgcSrc)
{
    GC_UNWRAP (pgcDst);
    (* pgcDst->funcs->CopyClip)(pgcDst, pgcSrc);
    GC_WRAP (pgcDst);
}

static void
VGAarbiterDestroyClip(GCPtr pGC)
{
    GC_UNWRAP (pGC);
    (* pGC->funcs->DestroyClip)(pGC);
    GC_WRAP (pGC);
}

/* GC Ops */
static void
VGAarbiterFillSpans(
    DrawablePtr pDraw,
    GC      *pGC,
    int     nInit,
    DDXPointPtr pptInit,
    int *pwidthInit,
    int fSorted )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->FillSpans)(pDraw, pGC, nInit, pptInit, pwidthInit, fSorted);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterSetSpans(
    DrawablePtr     pDraw,
    GCPtr       pGC,
    char        *pcharsrc,
    register DDXPointPtr ppt,
    int         *pwidth,
    int         nspans,
    int         fSorted )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->SetSpans)(pDraw, pGC, pcharsrc, ppt, pwidth, nspans, fSorted);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterPutImage(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     depth,
    int x, int y, int w, int h,
    int     leftPad,
    int     format,
    char    *pImage )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->PutImage)(pDraw, pGC, depth, x, y, w, h,
              leftPad, format, pImage);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static RegionPtr
VGAarbiterCopyArea(
    DrawablePtr pSrc,
    DrawablePtr pDst,
    GC *pGC,
    int srcx, int srcy,
    int width, int height,
    int dstx, int dsty )
{
    RegionPtr ret;

    GC_UNWRAP(pGC);
    VGAGet_GC();
    ret = (*pGC->ops->CopyArea)(pSrc, pDst,
                pGC, srcx, srcy, width, height, dstx, dsty);
    VGAPut_GC();
    GC_WRAP(pGC);
    return ret;
}

static RegionPtr
VGAarbiterCopyPlane(
    DrawablePtr pSrc,
    DrawablePtr pDst,
    GCPtr pGC,
    int srcx, int srcy,
    int width, int height,
    int dstx, int dsty,
    unsigned long bitPlane )
{
    RegionPtr ret;

    GC_UNWRAP(pGC);
    VGAGet_GC();
    ret = (*pGC->ops->CopyPlane)(pSrc, pDst, pGC, srcx, srcy,
                 width, height, dstx, dsty, bitPlane);
    VGAPut_GC();
    GC_WRAP(pGC);
    return ret;
}

static void
VGAarbiterPolyPoint(
    DrawablePtr pDraw,
    GCPtr pGC,
    int mode,
    int npt,
    xPoint *pptInit )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->PolyPoint)(pDraw, pGC, mode, npt, pptInit);
    VGAPut_GC();
    GC_WRAP(pGC);
}


static void
VGAarbiterPolylines(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     mode,
    int     npt,
    DDXPointPtr pptInit )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->Polylines)(pDraw, pGC, mode, npt, pptInit);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterPolySegment(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     nseg,
    xSegment    *pSeg )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->PolySegment)(pDraw, pGC, nseg, pSeg);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterPolyRectangle(
    DrawablePtr  pDraw,
    GCPtr        pGC,
    int          nRectsInit,
    xRectangle  *pRectsInit )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->PolyRectangle)(pDraw, pGC, nRectsInit, pRectsInit);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterPolyArc(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     narcs,
    xArc    *parcs )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->PolyArc)(pDraw, pGC, narcs, parcs);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterFillPolygon(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     shape,
    int     mode,
    int     count,
    DDXPointPtr ptsIn )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->FillPolygon)(pDraw, pGC, shape, mode, count, ptsIn);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterPolyFillRect(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     nrectFill,
    xRectangle  *prectInit)
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->PolyFillRect)(pDraw, pGC, nrectFill, prectInit);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterPolyFillArc(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     narcs,
    xArc    *parcs )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->PolyFillArc)(pDraw, pGC, narcs, parcs);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static int
VGAarbiterPolyText8(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     x,
    int     y,
    int     count,
    char    *chars )
{
    int ret;

    GC_UNWRAP(pGC);
    VGAGet_GC();
    ret = (*pGC->ops->PolyText8)(pDraw, pGC, x, y, count, chars);
    VGAPut_GC();
    GC_WRAP(pGC);
    return ret;
}

static int
VGAarbiterPolyText16(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     x,
    int     y,
    int     count,
    unsigned short *chars )
{
    int ret;

    GC_UNWRAP(pGC);
    VGAGet_GC();
    ret = (*pGC->ops->PolyText16)(pDraw, pGC, x, y, count, chars);
    VGAPut_GC();
    GC_WRAP(pGC);
    return ret;
}

static void
VGAarbiterImageText8(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     x,
    int     y,
    int     count,
    char    *chars )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->ImageText8)(pDraw, pGC, x, y, count, chars);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterImageText16(
    DrawablePtr pDraw,
    GCPtr   pGC,
    int     x,
    int     y,
    int     count,
    unsigned short *chars )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->ImageText16)(pDraw, pGC, x, y, count, chars);
    VGAPut_GC();
    GC_WRAP(pGC);
}


static void
VGAarbiterImageGlyphBlt(
    DrawablePtr pDraw,
    GCPtr pGC,
    int xInit, int yInit,
    unsigned int nglyph,
    CharInfoPtr *ppci,
    pointer pglyphBase )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->ImageGlyphBlt)(pDraw, pGC, xInit, yInit,
                   nglyph, ppci, pglyphBase);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterPolyGlyphBlt(
    DrawablePtr pDraw,
    GCPtr pGC,
    int xInit, int yInit,
    unsigned int nglyph,
    CharInfoPtr *ppci,
    pointer pglyphBase )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->PolyGlyphBlt)(pDraw, pGC, xInit, yInit,
                  nglyph, ppci, pglyphBase);
    VGAPut_GC();
    GC_WRAP(pGC);
}

static void
VGAarbiterPushPixels(
    GCPtr   pGC,
    PixmapPtr   pBitMap,
    DrawablePtr pDraw,
    int dx, int dy, int xOrg, int yOrg )
{
    GC_UNWRAP(pGC);
    VGAGet_GC();
    (*pGC->ops->PushPixels)(pGC, pBitMap, pDraw, dx, dy, xOrg, yOrg);
    VGAPut_GC();
    GC_WRAP(pGC);
}


/* miSpriteFuncs */
static Bool
VGAarbiterSpriteRealizeCursor(DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCur)
{
    Bool val;
    SPRITE_PROLOG;
    VGAGet();
    val = PointPriv->spriteFuncs->RealizeCursor(pDev, pScreen, pCur);
    VGAPut();
    SPRITE_EPILOG;
    return val;
}

static Bool
VGAarbiterSpriteUnrealizeCursor(DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCur)
{
    Bool val;
    SPRITE_PROLOG;
    VGAGet();
    val = PointPriv->spriteFuncs->UnrealizeCursor(pDev, pScreen, pCur);
    VGAPut();
    SPRITE_EPILOG;
    return val;
}

static void
VGAarbiterSpriteSetCursor(DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCur, int x, int y)
{
    SPRITE_PROLOG;
    VGAGet();
    PointPriv->spriteFuncs->SetCursor(pDev, pScreen, pCur, x, y);
    VGAPut();
    SPRITE_EPILOG;
}

static void
VGAarbiterSpriteMoveCursor(DeviceIntPtr pDev, ScreenPtr pScreen, int x, int y)
{
    SPRITE_PROLOG;
    VGAGet();
    PointPriv->spriteFuncs->MoveCursor(pDev, pScreen, x, y);
    VGAPut();
    SPRITE_EPILOG;
}

static Bool
VGAarbiterDeviceCursorInitialize(DeviceIntPtr pDev, ScreenPtr pScreen)
{
    Bool val;
    SPRITE_PROLOG;
    VGAGet();
    val = PointPriv->spriteFuncs->DeviceCursorInitialize(pDev, pScreen);
    VGAPut();
    SPRITE_EPILOG;
    return val;
}

static void
VGAarbiterDeviceCursorCleanup(DeviceIntPtr pDev, ScreenPtr pScreen)
{
    SPRITE_PROLOG;
    VGAGet();
    PointPriv->spriteFuncs->DeviceCursorCleanup(pDev, pScreen);
    VGAPut();
    SPRITE_EPILOG;
}

#ifdef RENDER
static void
VGAarbiterComposite(CARD8 op, PicturePtr pSrc, PicturePtr pMask,
         PicturePtr pDst, INT16 xSrc, INT16 ySrc, INT16 xMask,
         INT16 yMask, INT16 xDst, INT16 yDst, CARD16 width,
         CARD16 height)
{
    ScreenPtr       pScreen = pDst->pDrawable->pScreen;
    PictureScreenPtr    ps = GetPictureScreen(pScreen);

    PICTURE_PROLOGUE(Composite);

    VGAGet();
    (*ps->Composite) (op, pSrc, pMask, pDst, xSrc, ySrc, xMask, yMask, xDst,
              yDst, width, height);
    VGAPut();
    PICTURE_EPILOGUE(Composite, VGAarbiterComposite);
}

static void
VGAarbiterGlyphs(CARD8 op, PicturePtr pSrc, PicturePtr pDst,
      PictFormatPtr maskFormat, INT16 xSrc, INT16 ySrc, int nlist,
      GlyphListPtr list, GlyphPtr *glyphs)
{
    ScreenPtr       pScreen = pDst->pDrawable->pScreen;
    PictureScreenPtr    ps = GetPictureScreen(pScreen);

    PICTURE_PROLOGUE(Glyphs);

    VGAGet();
    (*ps->Glyphs)(op, pSrc, pDst, maskFormat, xSrc, ySrc, nlist, list, glyphs);
    VGAPut();
    PICTURE_EPILOGUE (Glyphs, VGAarbiterGlyphs);
}

static void
VGAarbiterCompositeRects(CARD8 op, PicturePtr pDst, xRenderColor *color, int nRect,
          xRectangle *rects)
{
    ScreenPtr       pScreen = pDst->pDrawable->pScreen;
    PictureScreenPtr    ps = GetPictureScreen(pScreen);

    PICTURE_PROLOGUE(CompositeRects);

    VGAGet();
    (*ps->CompositeRects)(op, pDst, color, nRect, rects);
    VGAPut();
    PICTURE_EPILOGUE (CompositeRects, VGAarbiterCompositeRects);
}
#endif
#else
/* dummy functions */
void xf86VGAarbiterInit(void) {}
void xf86VGAarbiterFini(void) {}

void xf86VGAarbiterLock(ScrnInfoPtr pScrn) {}
void xf86VGAarbiterUnlock(ScrnInfoPtr pScrn) {}
Bool xf86VGAarbiterAllowDRI(ScreenPtr pScreen) { return TRUE; }
void xf86VGAarbiterScrnInit(ScrnInfoPtr pScrn) {}
void xf86VGAarbiterDeviceDecodes(ScrnInfoPtr pScrn) {}
Bool xf86VGAarbiterWrapFunctions(void) { return FALSE; }

#endif
