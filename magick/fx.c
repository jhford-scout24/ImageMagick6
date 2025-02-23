/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%                                 FFFFF  X   X                                %
%                                 F       X X                                 %
%                                 FFF      X                                  %
%                                 F       X X                                 %
%                                 F      X   X                                %
%                                                                             %
%                                                                             %
%                   MagickCore Image Special Effects Methods                  %
%                                                                             %
%                               Software Design                               %
%                                    Cristy                                   %
%                                 October 1996                                %
%                                                                             %
%                                                                             %
%  Copyright 1999-2019 ImageMagick Studio LLC, a non-profit organization      %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    https://imagemagick.org/script/license.php                               %
%                                                                             %
%  Unless required by applicable law or agreed to in writing, software        %
%  distributed under the License is distributed on an "AS IS" BASIS,          %
%  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
%  See the License for the specific language governing permissions and        %
%  limitations under the License.                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%
%
*/

/*
  Include declarations.
*/
#include "magick/studio.h"
#include "magick/accelerate-private.h"
#include "magick/annotate.h"
#include "magick/artifact.h"
#include "magick/attribute.h"
#include "magick/cache.h"
#include "magick/cache-view.h"
#include "magick/channel.h"
#include "magick/color.h"
#include "magick/color-private.h"
#include "magick/colorspace.h"
#include "magick/colorspace-private.h"
#include "magick/composite.h"
#include "magick/decorate.h"
#include "magick/distort.h"
#include "magick/draw.h"
#include "magick/effect.h"
#include "magick/enhance.h"
#include "magick/exception.h"
#include "magick/exception-private.h"
#include "magick/fx.h"
#include "magick/fx-private.h"
#include "magick/gem.h"
#include "magick/geometry.h"
#include "magick/layer.h"
#include "magick/list.h"
#include "magick/log.h"
#include "magick/image.h"
#include "magick/image-private.h"
#include "magick/magick.h"
#include "magick/memory_.h"
#include "magick/monitor.h"
#include "magick/monitor-private.h"
#include "magick/opencl-private.h"
#include "magick/option.h"
#include "magick/pixel-accessor.h"
#include "magick/pixel-private.h"
#include "magick/property.h"
#include "magick/quantum.h"
#include "magick/quantum-private.h"
#include "magick/random_.h"
#include "magick/random-private.h"
#include "magick/resample.h"
#include "magick/resample-private.h"
#include "magick/resize.h"
#include "magick/resource_.h"
#include "magick/splay-tree.h"
#include "magick/statistic.h"
#include "magick/string_.h"
#include "magick/string-private.h"
#include "magick/thread-private.h"
#include "magick/threshold.h"
#include "magick/transform.h"
#include "magick/utility.h"

/*
  Define declarations.
*/
#define LeftShiftOperator  0xf5U
#define RightShiftOperator  0xf6U
#define LessThanEqualOperator  0xf7U
#define GreaterThanEqualOperator  0xf8U
#define EqualOperator  0xf9U
#define NotEqualOperator  0xfaU
#define LogicalAndOperator  0xfbU
#define LogicalOrOperator  0xfcU
#define ExponentialNotation  0xfdU

struct _FxInfo
{
  const Image
    *images;

  char
    *expression;

  FILE
    *file;

  SplayTreeInfo
    *colors,
    *symbols;

  CacheView
    **view;

  RandomInfo
    *random_info;

  ExceptionInfo
    *exception;
};

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   A c q u i r e F x I n f o                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AcquireFxInfo() allocates the FxInfo structure.
%
%  The format of the AcquireFxInfo method is:
%
%      FxInfo *AcquireFxInfo(Image *image,const char *expression)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o expression: the expression.
%
*/
MagickExport FxInfo *AcquireFxInfo(const Image *image,const char *expression)
{
  char
    fx_op[2];

  const Image
    *next;

  FxInfo
    *fx_info;

  register ssize_t
    i;

  fx_info=(FxInfo *) AcquireMagickMemory(sizeof(*fx_info));
  if (fx_info == (FxInfo *) NULL)
    ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
  (void) memset(fx_info,0,sizeof(*fx_info));
  fx_info->exception=AcquireExceptionInfo();
  fx_info->images=image;
  fx_info->colors=NewSplayTree(CompareSplayTreeString,RelinquishMagickMemory,
    RelinquishAlignedMemory);
  fx_info->symbols=NewSplayTree(CompareSplayTreeString,RelinquishMagickMemory,
    RelinquishMagickMemory);
  fx_info->view=(CacheView **) AcquireQuantumMemory(GetImageListLength(
    fx_info->images),sizeof(*fx_info->view));
  if (fx_info->view == (CacheView **) NULL)
    ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
  i=0;
  next=GetFirstImageInList(fx_info->images);
  for ( ; next != (Image *) NULL; next=next->next)
  {
    fx_info->view[i]=AcquireVirtualCacheView(next,fx_info->exception);
    i++;
  }
  fx_info->random_info=AcquireRandomInfo();
  fx_info->expression=ConstantString(expression);
  fx_info->file=stderr;
  (void) SubstituteString(&fx_info->expression," ","");  /* compact string */
  /*
    Force right-to-left associativity for unary negation.
  */
  (void) SubstituteString(&fx_info->expression,"-","-1.0*");
  (void) SubstituteString(&fx_info->expression,"^-1.0*","^-");
  (void) SubstituteString(&fx_info->expression,"E-1.0*","E-");
  (void) SubstituteString(&fx_info->expression,"e-1.0*","e-");
  /*
    Convert compound to simple operators.
  */
  fx_op[1]='\0';
  *fx_op=(char) LeftShiftOperator;
  (void) SubstituteString(&fx_info->expression,"<<",fx_op);
  *fx_op=(char) RightShiftOperator;
  (void) SubstituteString(&fx_info->expression,">>",fx_op);
  *fx_op=(char) LessThanEqualOperator;
  (void) SubstituteString(&fx_info->expression,"<=",fx_op);
  *fx_op=(char) GreaterThanEqualOperator;
  (void) SubstituteString(&fx_info->expression,">=",fx_op);
  *fx_op=(char) EqualOperator;
  (void) SubstituteString(&fx_info->expression,"==",fx_op);
  *fx_op=(char) NotEqualOperator;
  (void) SubstituteString(&fx_info->expression,"!=",fx_op);
  *fx_op=(char) LogicalAndOperator;
  (void) SubstituteString(&fx_info->expression,"&&",fx_op);
  *fx_op=(char) LogicalOrOperator;
  (void) SubstituteString(&fx_info->expression,"||",fx_op);
  *fx_op=(char) ExponentialNotation;
  (void) SubstituteString(&fx_info->expression,"**",fx_op);
  return(fx_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     A d d N o i s e I m a g e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AddNoiseImage() adds random noise to the image.
%
%  The format of the AddNoiseImage method is:
%
%      Image *AddNoiseImage(const Image *image,const NoiseType noise_type,
%        ExceptionInfo *exception)
%      Image *AddNoiseImageChannel(const Image *image,const ChannelType channel,
%        const NoiseType noise_type,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o channel: the channel type.
%
%    o noise_type:  The type of noise: Uniform, Gaussian, Multiplicative,
%      Impulse, Laplacian, or Poisson.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *AddNoiseImage(const Image *image,const NoiseType noise_type,
  ExceptionInfo *exception)
{
  Image
    *noise_image;

  noise_image=AddNoiseImageChannel(image,DefaultChannels,noise_type,exception);
  return(noise_image);
}

MagickExport Image *AddNoiseImageChannel(const Image *image,
  const ChannelType channel,const NoiseType noise_type,ExceptionInfo *exception)
{
#define AddNoiseImageTag  "AddNoise/Image"

  CacheView
    *image_view,
    *noise_view;

  const char
    *option;

  double
    attenuate;

  Image
    *noise_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  RandomInfo
    **magick_restrict random_info;

  ssize_t
    y;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
  unsigned long
    key;
#endif

  /*
    Initialize noise image attributes.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
#if defined(MAGICKCORE_OPENCL_SUPPORT)
  noise_image=AccelerateAddNoiseImage(image,channel,noise_type,exception);
  if (noise_image != (Image *) NULL)
    return(noise_image);
#endif
  noise_image=CloneImage(image,0,0,MagickTrue,exception);
  if (noise_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(noise_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&noise_image->exception);
      noise_image=DestroyImage(noise_image);
      return((Image *) NULL);
    }
  /*
    Add noise in each row.
  */
  attenuate=1.0;
  option=GetImageArtifact(image,"attenuate");
  if (option != (char *) NULL)
    attenuate=StringToDouble(option,(char **) NULL);
  status=MagickTrue;
  progress=0;
  random_info=AcquireRandomInfoThreadSet();
  image_view=AcquireVirtualCacheView(image,exception);
  noise_view=AcquireAuthenticCacheView(noise_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  key=GetRandomSecretKey(random_info[0]);
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,noise_image,image->rows,key == ~0UL)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    const int
      id = GetOpenMPThreadId();

    MagickBooleanType
      sync;

    register const IndexPacket
      *magick_restrict indexes;

    register const PixelPacket
      *magick_restrict p;

    register IndexPacket
      *magick_restrict noise_indexes;

    register ssize_t
      x;

    register PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    p=GetCacheViewVirtualPixels(image_view,0,y,image->columns,1,exception);
    q=GetCacheViewAuthenticPixels(noise_view,0,y,noise_image->columns,1,
      exception);
    if ((p == (PixelPacket *) NULL) || (q == (PixelPacket *) NULL))
      {
        status=MagickFalse;
        continue;
      }
    indexes=GetCacheViewVirtualIndexQueue(image_view);
    noise_indexes=GetCacheViewAuthenticIndexQueue(noise_view);
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      if ((channel & RedChannel) != 0)
        SetPixelRed(q,ClampToQuantum(GenerateDifferentialNoise(random_info[id],
          GetPixelRed(p),noise_type,attenuate)));
      if (IsGrayColorspace(image->colorspace) != MagickFalse)
        {
          SetPixelGreen(q,GetPixelRed(q));
          SetPixelBlue(q,GetPixelRed(q));
        }
      else
        {
          if ((channel & GreenChannel) != 0)
            SetPixelGreen(q,ClampToQuantum(GenerateDifferentialNoise(
              random_info[id],GetPixelGreen(p),noise_type,attenuate)));
          if ((channel & BlueChannel) != 0)
            SetPixelBlue(q,ClampToQuantum(GenerateDifferentialNoise(
              random_info[id],GetPixelBlue(p),noise_type,attenuate)));
         }
      if ((channel & OpacityChannel) != 0)
        SetPixelOpacity(q,ClampToQuantum(GenerateDifferentialNoise(
          random_info[id],GetPixelOpacity(p),noise_type,attenuate)));
      if (((channel & IndexChannel) != 0) &&
          (image->colorspace == CMYKColorspace))
        SetPixelIndex(noise_indexes+x,ClampToQuantum(
          GenerateDifferentialNoise(random_info[id],GetPixelIndex(
          indexes+x),noise_type,attenuate)));
      p++;
      q++;
    }
    sync=SyncCacheViewAuthenticPixels(noise_view,exception);
    if (sync == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,AddNoiseImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  noise_view=DestroyCacheView(noise_view);
  image_view=DestroyCacheView(image_view);
  random_info=DestroyRandomInfoThreadSet(random_info);
  if (status == MagickFalse)
    noise_image=DestroyImage(noise_image);
  return(noise_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     B l u e S h i f t I m a g e                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  BlueShiftImage() mutes the colors of the image to simulate a scene at
%  nighttime in the moonlight.
%
%  The format of the BlueShiftImage method is:
%
%      Image *BlueShiftImage(const Image *image,const double factor,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o factor: the shift factor.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *BlueShiftImage(const Image *image,const double factor,
  ExceptionInfo *exception)
{
#define BlueShiftImageTag  "BlueShift/Image"

  CacheView
    *image_view,
    *shift_view;

  Image
    *shift_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  ssize_t
    y;

  /*
    Allocate blue shift image.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  shift_image=CloneImage(image,0,0,MagickTrue,exception);
  if (shift_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(shift_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&shift_image->exception);
      shift_image=DestroyImage(shift_image);
      return((Image *) NULL);
    }
  /*
    Blue-shift DirectClass image.
  */
  status=MagickTrue;
  progress=0;
  image_view=AcquireVirtualCacheView(image,exception);
  shift_view=AcquireAuthenticCacheView(shift_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,shift_image,image->rows,1)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    MagickBooleanType
      sync;

    MagickPixelPacket
      pixel;

    Quantum
      quantum;

    register const PixelPacket
      *magick_restrict p;

    register ssize_t
      x;

    register PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    p=GetCacheViewVirtualPixels(image_view,0,y,image->columns,1,exception);
    q=QueueCacheViewAuthenticPixels(shift_view,0,y,shift_image->columns,1,
      exception);
    if ((p == (const PixelPacket *) NULL) || (q == (PixelPacket *) NULL))
      {
        status=MagickFalse;
        continue;
      }
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      quantum=GetPixelRed(p);
      if (GetPixelGreen(p) < quantum)
        quantum=GetPixelGreen(p);
      if (GetPixelBlue(p) < quantum)
        quantum=GetPixelBlue(p);
      pixel.red=0.5*(GetPixelRed(p)+factor*quantum);
      pixel.green=0.5*(GetPixelGreen(p)+factor*quantum);
      pixel.blue=0.5*(GetPixelBlue(p)+factor*quantum);
      quantum=GetPixelRed(p);
      if (GetPixelGreen(p) > quantum)
        quantum=GetPixelGreen(p);
      if (GetPixelBlue(p) > quantum)
        quantum=GetPixelBlue(p);
      pixel.red=0.5*(pixel.red+factor*quantum);
      pixel.green=0.5*(pixel.green+factor*quantum);
      pixel.blue=0.5*(pixel.blue+factor*quantum);
      SetPixelRed(q,ClampToQuantum(pixel.red));
      SetPixelGreen(q,ClampToQuantum(pixel.green));
      SetPixelBlue(q,ClampToQuantum(pixel.blue));
      p++;
      q++;
    }
    sync=SyncCacheViewAuthenticPixels(shift_view,exception);
    if (sync == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,BlueShiftImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  image_view=DestroyCacheView(image_view);
  shift_view=DestroyCacheView(shift_view);
  if (status == MagickFalse)
    shift_image=DestroyImage(shift_image);
  return(shift_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     C h a r c o a l I m a g e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  CharcoalImage() creates a new image that is a copy of an existing one with
%  the edge highlighted.  It allocates the memory necessary for the new Image
%  structure and returns a pointer to the new image.
%
%  The format of the CharcoalImage method is:
%
%      Image *CharcoalImage(const Image *image,const double radius,
%        const double sigma,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o radius: the radius of the pixel neighborhood.
%
%    o sigma: the standard deviation of the Gaussian, in pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *CharcoalImage(const Image *image,const double radius,
  const double sigma,ExceptionInfo *exception)
{
  Image
    *charcoal_image,
    *edge_image;

  MagickBooleanType
    status;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  edge_image=EdgeImage(image,radius,exception);
  if (edge_image == (Image *) NULL)
    return((Image *) NULL);
  charcoal_image=(Image *) NULL;
  status=ClampImage(edge_image);
  if (status != MagickFalse)
    charcoal_image=BlurImage(edge_image,radius,sigma,exception);
  edge_image=DestroyImage(edge_image);
  if (charcoal_image == (Image *) NULL)
    return((Image *) NULL);
  status=NormalizeImage(charcoal_image);
  if (status != MagickFalse)
    status=NegateImage(charcoal_image,MagickFalse);
  if (status != MagickFalse)
    status=GrayscaleImage(charcoal_image,image->intensity);
  if (status == MagickFalse)
    charcoal_image=DestroyImage(charcoal_image);
  return(charcoal_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     C o l o r i z e I m a g e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ColorizeImage() blends the fill color with each pixel in the image.
%  A percentage blend is specified with opacity.  Control the application
%  of different color components by specifying a different percentage for
%  each component (e.g. 90/100/10 is 90% red, 100% green, and 10% blue).
%
%  The format of the ColorizeImage method is:
%
%      Image *ColorizeImage(const Image *image,const char *opacity,
%        const PixelPacket colorize,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o opacity:  A character string indicating the level of opacity as a
%      percentage.
%
%    o colorize: A color value.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *ColorizeImage(const Image *image,const char *opacity,
  const PixelPacket colorize,ExceptionInfo *exception)
{
#define ColorizeImageTag  "Colorize/Image"

  CacheView
    *colorize_view,
    *image_view;

  GeometryInfo
    geometry_info;

  Image
    *colorize_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  MagickPixelPacket
    pixel;

  MagickStatusType
    flags;

  ssize_t
    y;

  /*
    Allocate colorized image.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  colorize_image=CloneImage(image,0,0,MagickTrue,exception);
  if (colorize_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(colorize_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&colorize_image->exception);
      colorize_image=DestroyImage(colorize_image);
      return((Image *) NULL);
    }
  if ((IsGrayColorspace(image->colorspace) != MagickFalse) ||
      (IsPixelGray(&colorize) != MagickFalse))
    (void) SetImageColorspace(colorize_image,sRGBColorspace);
  if ((colorize_image->matte == MagickFalse) &&
      (colorize.opacity != OpaqueOpacity))
    (void) SetImageAlphaChannel(colorize_image,OpaqueAlphaChannel);
  if (opacity == (const char *) NULL)
    return(colorize_image);
  /*
    Determine RGB values of the pen color.
  */
  flags=ParseGeometry(opacity,&geometry_info);
  pixel.red=geometry_info.rho;
  pixel.green=geometry_info.rho;
  pixel.blue=geometry_info.rho;
  pixel.opacity=(MagickRealType) OpaqueOpacity;
  if ((flags & SigmaValue) != 0)
    pixel.green=geometry_info.sigma;
  if ((flags & XiValue) != 0)
    pixel.blue=geometry_info.xi;
  if ((flags & PsiValue) != 0)
    pixel.opacity=geometry_info.psi;
  /*
    Colorize DirectClass image.
  */
  status=MagickTrue;
  progress=0;
  image_view=AcquireVirtualCacheView(image,exception);
  colorize_view=AcquireAuthenticCacheView(colorize_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,colorize_image,image->rows,1)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    MagickBooleanType
      sync;

    register const PixelPacket
      *magick_restrict p;

    register ssize_t
      x;

    register PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    p=GetCacheViewVirtualPixels(image_view,0,y,image->columns,1,exception);
    q=QueueCacheViewAuthenticPixels(colorize_view,0,y,colorize_image->columns,1,
      exception);
    if ((p == (const PixelPacket *) NULL) || (q == (PixelPacket *) NULL))
      {
        status=MagickFalse;
        continue;
      }
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      SetPixelRed(q,((GetPixelRed(p)*(100.0-pixel.red)+
        colorize.red*pixel.red)/100.0));
      SetPixelGreen(q,((GetPixelGreen(p)*(100.0-pixel.green)+
        colorize.green*pixel.green)/100.0));
      SetPixelBlue(q,((GetPixelBlue(p)*(100.0-pixel.blue)+
        colorize.blue*pixel.blue)/100.0));
      if (colorize_image->matte == MagickFalse)
        SetPixelOpacity(q,GetPixelOpacity(p));
      else
        SetPixelOpacity(q,((GetPixelOpacity(p)*(100.0-pixel.opacity)+
          colorize.opacity*pixel.opacity)/100.0));
      p++;
      q++;
    }
    sync=SyncCacheViewAuthenticPixels(colorize_view,exception);
    if (sync == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,ColorizeImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  image_view=DestroyCacheView(image_view);
  colorize_view=DestroyCacheView(colorize_view);
  if (status == MagickFalse)
    colorize_image=DestroyImage(colorize_image);
  return(colorize_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     C o l o r M a t r i x I m a g e                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ColorMatrixImage() applies color transformation to an image. This method
%  permits saturation changes, hue rotation, luminance to alpha, and various
%  other effects.  Although variable-sized transformation matrices can be used,
%  typically one uses a 5x5 matrix for an RGBA image and a 6x6 for CMYKA
%  (or RGBA with offsets).  The matrix is similar to those used by Adobe Flash
%  except offsets are in column 6 rather than 5 (in support of CMYKA images)
%  and offsets are normalized (divide Flash offset by 255).
%
%  The format of the ColorMatrixImage method is:
%
%      Image *ColorMatrixImage(const Image *image,
%        const KernelInfo *color_matrix,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o color_matrix:  the color matrix.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *ColorMatrixImage(const Image *image,
  const KernelInfo *color_matrix,ExceptionInfo *exception)
{
#define ColorMatrixImageTag  "ColorMatrix/Image"

  CacheView
    *color_view,
    *image_view;

  double
    ColorMatrix[6][6] =
    {
      { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
      { 0.0, 1.0, 0.0, 0.0, 0.0, 0.0 },
      { 0.0, 0.0, 1.0, 0.0, 0.0, 0.0 },
      { 0.0, 0.0, 0.0, 1.0, 0.0, 0.0 },
      { 0.0, 0.0, 0.0, 0.0, 1.0, 0.0 },
      { 0.0, 0.0, 0.0, 0.0, 0.0, 1.0 }
    };

  Image
    *color_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  register ssize_t
    i;

  ssize_t
    u,
    v,
    y;

  /*
    Create color matrix.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  i=0;
  for (v=0; v < (ssize_t) color_matrix->height; v++)
    for (u=0; u < (ssize_t) color_matrix->width; u++)
    {
      if ((v < 6) && (u < 6))
        ColorMatrix[v][u]=color_matrix->values[i];
      i++;
    }
  /*
    Initialize color image.
  */
  color_image=CloneImage(image,0,0,MagickTrue,exception);
  if (color_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(color_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&color_image->exception);
      color_image=DestroyImage(color_image);
      return((Image *) NULL);
    }
  if (image->debug != MagickFalse)
    {
      char
        format[MaxTextExtent],
        *message;

      (void) LogMagickEvent(TransformEvent,GetMagickModule(),
        "  ColorMatrix image with color matrix:");
      message=AcquireString("");
      for (v=0; v < 6; v++)
      {
        *message='\0';
        (void) FormatLocaleString(format,MaxTextExtent,"%.20g: ",(double) v);
        (void) ConcatenateString(&message,format);
        for (u=0; u < 6; u++)
        {
          (void) FormatLocaleString(format,MaxTextExtent,"%+f ",
            ColorMatrix[v][u]);
          (void) ConcatenateString(&message,format);
        }
        (void) LogMagickEvent(TransformEvent,GetMagickModule(),"%s",message);
      }
      message=DestroyString(message);
    }
  /*
    ColorMatrix image.
  */
  status=MagickTrue;
  progress=0;
  image_view=AcquireVirtualCacheView(image,exception);
  color_view=AcquireAuthenticCacheView(color_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,color_image,image->rows,1)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    MagickRealType
      pixel;

    register const IndexPacket
      *magick_restrict indexes;

    register const PixelPacket
      *magick_restrict p;

    register ssize_t
      x;

    register IndexPacket
      *magick_restrict color_indexes;

    register PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    p=GetCacheViewVirtualPixels(image_view,0,y,image->columns,1,exception);
    q=GetCacheViewAuthenticPixels(color_view,0,y,color_image->columns,1,
      exception);
    if ((p == (const PixelPacket *) NULL) || (q == (PixelPacket *) NULL))
      {
        status=MagickFalse;
        continue;
      }
    indexes=GetCacheViewVirtualIndexQueue(image_view);
    color_indexes=GetCacheViewAuthenticIndexQueue(color_view);
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      register ssize_t
        v;

      size_t
        height;

      height=color_matrix->height > 6 ? 6UL : color_matrix->height;
      for (v=0; v < (ssize_t) height; v++)
      {
        pixel=ColorMatrix[v][0]*GetPixelRed(p)+ColorMatrix[v][1]*
          GetPixelGreen(p)+ColorMatrix[v][2]*GetPixelBlue(p);
        if (image->matte != MagickFalse)
          pixel+=ColorMatrix[v][3]*(QuantumRange-GetPixelOpacity(p));
        if (image->colorspace == CMYKColorspace)
          pixel+=ColorMatrix[v][4]*GetPixelIndex(indexes+x);
        pixel+=QuantumRange*ColorMatrix[v][5];
        switch (v)
        {
          case 0: SetPixelRed(q,ClampToQuantum(pixel)); break;
          case 1: SetPixelGreen(q,ClampToQuantum(pixel)); break;
          case 2: SetPixelBlue(q,ClampToQuantum(pixel)); break;
          case 3:
          {
            if (image->matte != MagickFalse)
              SetPixelAlpha(q,ClampToQuantum(pixel));
            break;
          }
          case 4:
          {
            if (image->colorspace == CMYKColorspace)
              SetPixelIndex(color_indexes+x,ClampToQuantum(pixel));
            break;
          }
        }
      }
      p++;
      q++;
    }
    if (SyncCacheViewAuthenticPixels(color_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,ColorMatrixImageTag,progress,
          image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  color_view=DestroyCacheView(color_view);
  image_view=DestroyCacheView(image_view);
  if (status == MagickFalse)
    color_image=DestroyImage(color_image);
  return(color_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D e s t r o y F x I n f o                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DestroyFxInfo() deallocates memory associated with an FxInfo structure.
%
%  The format of the DestroyFxInfo method is:
%
%      ImageInfo *DestroyFxInfo(ImageInfo *fx_info)
%
%  A description of each parameter follows:
%
%    o fx_info: the fx info.
%
*/
MagickExport FxInfo *DestroyFxInfo(FxInfo *fx_info)
{
  register ssize_t
    i;

  fx_info->exception=DestroyExceptionInfo(fx_info->exception);
  fx_info->expression=DestroyString(fx_info->expression);
  fx_info->symbols=DestroySplayTree(fx_info->symbols);
  fx_info->colors=DestroySplayTree(fx_info->colors);
  for (i=(ssize_t) GetImageListLength(fx_info->images)-1; i >= 0; i--)
    fx_info->view[i]=DestroyCacheView(fx_info->view[i]);
  fx_info->view=(CacheView **) RelinquishMagickMemory(fx_info->view);
  fx_info->random_info=DestroyRandomInfo(fx_info->random_info);
  fx_info=(FxInfo *) RelinquishMagickMemory(fx_info);
  return(fx_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+     F x E v a l u a t e C h a n n e l E x p r e s s i o n                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  FxEvaluateChannelExpression() evaluates an expression and returns the
%  results.
%
%  The format of the FxEvaluateExpression method is:
%
%      MagickBooleanType FxEvaluateChannelExpression(FxInfo *fx_info,
%        const ChannelType channel,const ssize_t x,const ssize_t y,
%        double *alpha,Exceptioninfo *exception)
%      MagickBooleanType FxEvaluateExpression(FxInfo *fx_info,double *alpha,
%        Exceptioninfo *exception)
%
%  A description of each parameter follows:
%
%    o fx_info: the fx info.
%
%    o channel: the channel.
%
%    o x,y: the pixel position.
%
%    o alpha: the result.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static double FxChannelStatistics(FxInfo *fx_info,const Image *image,
  ChannelType channel,const char *symbol,ExceptionInfo *exception)
{
  char
    channel_symbol[MaxTextExtent],
    key[MaxTextExtent],
    statistic[MaxTextExtent];

  const char
    *value;

  register const char
    *p;

  for (p=symbol; (*p != '.') && (*p != '\0'); p++) ;
  *channel_symbol='\0';
  if (*p == '.')
    {
      ssize_t
        option;

      (void) CopyMagickString(channel_symbol,p+1,MaxTextExtent);
      option=ParseCommandOption(MagickChannelOptions,MagickTrue,channel_symbol);
      if (option >= 0)
        channel=(ChannelType) option;
    }
  (void) FormatLocaleString(key,MaxTextExtent,"%p.%.20g.%s",(void *) image,
    (double) channel,symbol);
  value=(const char *) GetValueFromSplayTree(fx_info->symbols,key);
  if (value != (const char *) NULL)
    return(QuantumScale*StringToDouble(value,(char **) NULL));
  (void) DeleteNodeFromSplayTree(fx_info->symbols,key);
  if (LocaleNCompare(symbol,"depth",5) == 0)
    {
      size_t
        depth;

      depth=GetImageChannelDepth(image,channel,exception);
      (void) FormatLocaleString(statistic,MaxTextExtent,"%.20g",(double) depth);
    }
  if (LocaleNCompare(symbol,"kurtosis",8) == 0)
    {
      double
        kurtosis,
        skewness;

      (void) GetImageChannelKurtosis(image,channel,&kurtosis,&skewness,
        exception);
      (void) FormatLocaleString(statistic,MaxTextExtent,"%.20g",kurtosis);
    }
  if (LocaleNCompare(symbol,"maxima",6) == 0)
    {
      double
        maxima,
        minima;

      (void) GetImageChannelRange(image,channel,&minima,&maxima,exception);
      (void) FormatLocaleString(statistic,MaxTextExtent,"%.20g",maxima);
    }
  if (LocaleNCompare(symbol,"mean",4) == 0)
    {
      double
        mean,
        standard_deviation;

      (void) GetImageChannelMean(image,channel,&mean,&standard_deviation,
        exception);
      (void) FormatLocaleString(statistic,MaxTextExtent,"%.20g",mean);
    }
  if (LocaleNCompare(symbol,"minima",6) == 0)
    {
      double
        maxima,
        minima;

      (void) GetImageChannelRange(image,channel,&minima,&maxima,exception);
      (void) FormatLocaleString(statistic,MaxTextExtent,"%.20g",minima);
    }
  if (LocaleNCompare(symbol,"skewness",8) == 0)
    {
      double
        kurtosis,
        skewness;

      (void) GetImageChannelKurtosis(image,channel,&kurtosis,&skewness,
        exception);
      (void) FormatLocaleString(statistic,MaxTextExtent,"%.20g",skewness);
    }
  if (LocaleNCompare(symbol,"standard_deviation",18) == 0)
    {
      double
        mean,
        standard_deviation;

      (void) GetImageChannelMean(image,channel,&mean,&standard_deviation,
        exception);
      (void) FormatLocaleString(statistic,MaxTextExtent,"%.20g",
        standard_deviation);
    }
  (void) AddValueToSplayTree(fx_info->symbols,ConstantString(key),
    ConstantString(statistic));
  return(QuantumScale*StringToDouble(statistic,(char **) NULL));
}

static double
  FxEvaluateSubexpression(FxInfo *,const ChannelType,const ssize_t,
    const ssize_t,const char *,const size_t,double *,ExceptionInfo *);

static inline MagickBooleanType IsFxFunction(const char *expression,
  const char *name,const size_t length)
{
  int
    c;

  c=expression[length];
  if ((LocaleNCompare(expression,name,length) == 0) &&
      ((isspace(c) != 0) || (c == '(')))
    return(MagickTrue);
  return(MagickFalse);
}

static MagickOffsetType FxGCD(MagickOffsetType alpha,MagickOffsetType beta)
{
  if (beta != 0)
    return(FxGCD(beta,alpha % beta));
  return(alpha);
}

static inline const char *FxSubexpression(const char *expression,
  ExceptionInfo *exception)
{
  const char
    *subexpression;

  register ssize_t
    level;

  level=0;
  subexpression=expression;
  while ((*subexpression != '\0') &&
         ((level != 1) || (strchr(")",(int) *subexpression) == (char *) NULL)))
  {
    if (strchr("(",(int) *subexpression) != (char *) NULL)
      level++;
    else
      if (strchr(")",(int) *subexpression) != (char *) NULL)
        level--;
    subexpression++;
  }
  if (*subexpression == '\0')
    (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
      "UnbalancedParenthesis","`%s'",expression);
  return(subexpression);
}

static double FxGetSymbol(FxInfo *fx_info,const ChannelType channel,
  const ssize_t x,const ssize_t y,const char *expression,const size_t depth,
  ExceptionInfo *exception)
{
  char
    *q,
    symbol[MaxTextExtent];

  const char
    *p,
    *value;

  double
    alpha,
    beta;

  Image
    *image;

  MagickBooleanType
    status;

  MagickPixelPacket
    pixel;

  PointInfo
    point;

  register ssize_t
    i;

  size_t
    level;

  p=expression;
  i=GetImageIndexInList(fx_info->images);
  level=0;
  point.x=(double) x;
  point.y=(double) y;
  if (isalpha((int) ((unsigned char) *(p+1))) == 0)
    {
      char
        *subexpression;

      subexpression=AcquireString(expression);
      if (strchr("suv",(int) *p) != (char *) NULL)
        {
          switch (*p)
          {
            case 's':
            default:
            {
              i=GetImageIndexInList(fx_info->images);
              break;
            }
            case 'u': i=0; break;
            case 'v': i=1; break;
          }
          p++;
          if (*p == '[')
            {
              level++;
              q=subexpression;
              for (p++; *p != '\0'; )
              {
                if (*p == '[')
                  level++;
                else
                  if (*p == ']')
                    {
                      level--;
                      if (level == 0)
                        break;
                    }
                *q++=(*p++);
              }
              *q='\0';
              alpha=FxEvaluateSubexpression(fx_info,channel,x,y,subexpression,
                depth,&beta,exception);
              i=(ssize_t) alpha;
              if (*p != '\0')
                p++;
            }
          if (*p == '.')
            p++;
        }
      if ((*p == 'p') && (isalpha((int) ((unsigned char) *(p+1))) == 0))
        {
          p++;
          if (*p == '{')
            {
              level++;
              q=subexpression;
              for (p++; *p != '\0'; )
              {
                if (*p == '{')
                  level++;
                else
                  if (*p == '}')
                    {
                      level--;
                      if (level == 0)
                        break;
                    }
                *q++=(*p++);
              }
              *q='\0';
              alpha=FxEvaluateSubexpression(fx_info,channel,x,y,subexpression,
                depth,&beta,exception);
              point.x=alpha;
              point.y=beta;
              if (*p != '\0')
                p++;
            }
          else
            if (*p == '[')
              {
                level++;
                q=subexpression;
                for (p++; *p != '\0'; )
                {
                  if (*p == '[')
                    level++;
                  else
                    if (*p == ']')
                      {
                        level--;
                        if (level == 0)
                          break;
                      }
                  *q++=(*p++);
                }
                *q='\0';
                alpha=FxEvaluateSubexpression(fx_info,channel,x,y,subexpression,
                  depth,&beta,exception);
                point.x+=alpha;
                point.y+=beta;
                if (*p != '\0')
                  p++;
              }
          if (*p == '.')
            p++;
        }
      subexpression=DestroyString(subexpression);
    }
  image=GetImageFromList(fx_info->images,i);
  if (image == (Image *) NULL)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
        "NoSuchImage","`%s'",expression);
      return(0.0);
    }
  i=GetImageIndexInList(image);
  GetMagickPixelPacket(image,&pixel);
  status=InterpolateMagickPixelPacket(image,fx_info->view[i],image->interpolate,
    point.x,point.y,&pixel,exception);
  (void) status;
  if ((strlen(p) > 2) &&
      (LocaleCompare(p,"intensity") != 0) &&
      (LocaleCompare(p,"luma") != 0) &&
      (LocaleCompare(p,"luminance") != 0) &&
      (LocaleCompare(p,"hue") != 0) &&
      (LocaleCompare(p,"saturation") != 0) &&
      (LocaleCompare(p,"lightness") != 0))
    {
      char
        name[MaxTextExtent];

      (void) CopyMagickString(name,p,MaxTextExtent);
      for (q=name+(strlen(name)-1); q > name; q--)
      {
        if (*q == ')')
          break;
        if (*q == '.')
          {
            *q='\0';
            break;
          }
      }
      if ((strlen(name) > 2) &&
          (GetValueFromSplayTree(fx_info->symbols,name) == (const char *) NULL))
        {
          MagickPixelPacket
            *color;

          color=(MagickPixelPacket *) GetValueFromSplayTree(fx_info->colors,
            name);
          if (color != (MagickPixelPacket *) NULL)
            {
              pixel=(*color);
              p+=strlen(name);
            }
          else
            if (QueryMagickColor(name,&pixel,fx_info->exception) != MagickFalse)
              {
                (void) AddValueToSplayTree(fx_info->colors,ConstantString(name),
                  CloneMagickPixelPacket(&pixel));
                p+=strlen(name);
              }
        }
    }
  (void) CopyMagickString(symbol,p,MaxTextExtent);
  StripString(symbol);
  if (*symbol == '\0')
    {
      switch (channel)
      {
        case RedChannel: return(QuantumScale*pixel.red);
        case GreenChannel: return(QuantumScale*pixel.green);
        case BlueChannel: return(QuantumScale*pixel.blue);
        case OpacityChannel:
        {
          double
            alpha;

          if (pixel.matte == MagickFalse)
            return(1.0);
          alpha=(double) (QuantumScale*GetPixelAlpha(&pixel));
          return(alpha);
        }
        case IndexChannel:
        {
          if (image->colorspace != CMYKColorspace)
            {
              (void) ThrowMagickException(exception,GetMagickModule(),
                ImageError,"ColorSeparatedImageRequired","`%s'",
                image->filename);
              return(0.0);
            }
          return(QuantumScale*pixel.index);
        }
        case DefaultChannels:
          return(QuantumScale*GetMagickPixelIntensity(image,&pixel));
        default:
          break;
      }
      (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
        "UnableToParseExpression","`%s'",p);
      return(0.0);
    }
  switch (*symbol)
  {
    case 'A':
    case 'a':
    {
      if (LocaleCompare(symbol,"a") == 0)
        return((double) (QuantumScale*GetPixelAlpha(&pixel)));
      break;
    }
    case 'B':
    case 'b':
    {
      if (LocaleCompare(symbol,"b") == 0)
        return(QuantumScale*pixel.blue);
      break;
    }
    case 'C':
    case 'c':
    {
      if (IsFxFunction(symbol,"channel",7) != MagickFalse)
        {
          GeometryInfo
            channel_info;

          MagickStatusType
            flags;

          flags=ParseGeometry(symbol+7,&channel_info);
          if (image->colorspace == CMYKColorspace)
            switch (channel)
            {
              case CyanChannel:
              {
                if ((flags & RhoValue) == 0)
                  return(0.0);
                return(channel_info.rho);
              }
              case MagentaChannel:
              {
                if ((flags & SigmaValue) == 0)
                  return(0.0);
                return(channel_info.sigma);
              }
              case YellowChannel:
              {
                if ((flags & XiValue) == 0)
                  return(0.0);
                return(channel_info.xi);
              }
              case BlackChannel:
              {
                if ((flags & PsiValue) == 0)
                  return(0.0);
                return(channel_info.psi);
              }
              case OpacityChannel:
              {
                if ((flags & ChiValue) == 0)
                  return(0.0);
                return(channel_info.chi);
              }
              default:
                return(0.0);
            }
          switch (channel)
          {
            case RedChannel:
            {
              if ((flags & RhoValue) == 0)
                return(0.0);
              return(channel_info.rho);
            }
            case GreenChannel:
            {
              if ((flags & SigmaValue) == 0)
                return(0.0);
              return(channel_info.sigma);
            }
            case BlueChannel:
            {
              if ((flags & XiValue) == 0)
                return(0.0);
              return(channel_info.xi);
            }
            case OpacityChannel:
            {
              if ((flags & PsiValue) == 0)
                return(0.0);
              return(channel_info.psi);
            }
            case IndexChannel:
            {
              if ((flags & ChiValue) == 0)
                return(0.0);
              return(channel_info.chi);
            }
            default:
              return(0.0);
          }
        }
      if (LocaleCompare(symbol,"c") == 0)
        return(QuantumScale*pixel.red);
      break;
    }
    case 'D':
    case 'd':
    {
      if (LocaleNCompare(symbol,"depth",5) == 0)
        return(FxChannelStatistics(fx_info,image,channel,symbol,exception));
      break;
    }
    case 'E':
    case 'e':
    {
      if (LocaleCompare(symbol,"extent") == 0)
        {
          if (image->extent != 0)
            return((double) image->extent);
          return((double) GetBlobSize(image));
        }
      break;
    }
    case 'G':
    case 'g':
    {
      if (LocaleCompare(symbol,"g") == 0)
        return(QuantumScale*pixel.green);
      break;
    }
    case 'K':
    case 'k':
    {
      if (LocaleNCompare(symbol,"kurtosis",8) == 0)
        return(FxChannelStatistics(fx_info,image,channel,symbol,exception));
      if (LocaleCompare(symbol,"k") == 0)
        {
          if (image->colorspace != CMYKColorspace)
            {
              (void) ThrowMagickException(exception,GetMagickModule(),
                OptionError,"ColorSeparatedImageRequired","`%s'",
                image->filename);
              return(0.0);
            }
          return(QuantumScale*pixel.index);
        }
      break;
    }
    case 'H':
    case 'h':
    {
      if (LocaleCompare(symbol,"h") == 0)
        return((double) image->rows);
      if (LocaleCompare(symbol,"hue") == 0)
        {
          double
            hue,
            lightness,
            saturation;

          ConvertRGBToHSL(ClampToQuantum(pixel.red),ClampToQuantum(pixel.green),
            ClampToQuantum(pixel.blue),&hue,&saturation,&lightness);
          return(hue);
        }
      break;
    }
    case 'I':
    case 'i':
    {
      if ((LocaleCompare(symbol,"image.depth") == 0) ||
          (LocaleCompare(symbol,"image.minima") == 0) ||
          (LocaleCompare(symbol,"image.maxima") == 0) ||
          (LocaleCompare(symbol,"image.mean") == 0) ||
          (LocaleCompare(symbol,"image.kurtosis") == 0) ||
          (LocaleCompare(symbol,"image.skewness") == 0) ||
          (LocaleCompare(symbol,"image.standard_deviation") == 0))
        return(FxChannelStatistics(fx_info,image,channel,symbol+6,exception));
      if (LocaleCompare(symbol,"image.resolution.x") == 0)
        return(image->x_resolution);
      if (LocaleCompare(symbol,"image.resolution.y") == 0)
        return(image->y_resolution);
      if (LocaleCompare(symbol,"intensity") == 0)
        return(QuantumScale*GetMagickPixelIntensity(image,&pixel));
      if (LocaleCompare(symbol,"i") == 0)
        return((double) x);
      break;
    }
    case 'J':
    case 'j':
    {
      if (LocaleCompare(symbol,"j") == 0)
        return((double) y);
      break;
    }
    case 'L':
    case 'l':
    {
      if (LocaleCompare(symbol,"lightness") == 0)
        {
          double
            hue,
            lightness,
            saturation;

          ConvertRGBToHSL(ClampToQuantum(pixel.red),ClampToQuantum(pixel.green),
            ClampToQuantum(pixel.blue),&hue,&saturation,&lightness);
          return(lightness);
        }
      if (LocaleCompare(symbol,"luma") == 0)
        {
          double
            luma;

          luma=0.212656*pixel.red+0.715158*pixel.green+0.072186*pixel.blue;
          return(QuantumScale*luma);
        }
      if (LocaleCompare(symbol,"luminance") == 0)
        {
          double
            luminance;

          luminance=0.212656*pixel.red+0.715158*pixel.green+0.072186*pixel.blue;
          return(QuantumScale*luminance);
        }
      break;
    }
    case 'M':
    case 'm':
    {
      if (LocaleNCompare(symbol,"maxima",6) == 0)
        return(FxChannelStatistics(fx_info,image,channel,symbol,exception));
      if (LocaleNCompare(symbol,"mean",4) == 0)
        return(FxChannelStatistics(fx_info,image,channel,symbol,exception));
      if (LocaleNCompare(symbol,"minima",6) == 0)
        return(FxChannelStatistics(fx_info,image,channel,symbol,exception));
      if (LocaleCompare(symbol,"m") == 0)
        return(QuantumScale*pixel.green);
      break;
    }
    case 'N':
    case 'n':
    {
      if (LocaleCompare(symbol,"n") == 0)
        return((double) GetImageListLength(fx_info->images));
      break;
    }
    case 'O':
    case 'o':
    {
      if (LocaleCompare(symbol,"o") == 0)
        return(QuantumScale*pixel.opacity);
      break;
    }
    case 'P':
    case 'p':
    {
      if (LocaleCompare(symbol,"page.height") == 0)
        return((double) image->page.height);
      if (LocaleCompare(symbol,"page.width") == 0)
        return((double) image->page.width);
      if (LocaleCompare(symbol,"page.x") == 0)
        return((double) image->page.x);
      if (LocaleCompare(symbol,"page.y") == 0)
        return((double) image->page.y);
      if (LocaleCompare(symbol,"printsize.x") == 0)
        return(PerceptibleReciprocal(image->x_resolution)*image->columns);
      if (LocaleCompare(symbol,"printsize.y") == 0)
        return(PerceptibleReciprocal(image->y_resolution)*image->rows);
      break;
    }
    case 'Q':
    case 'q':
    {
      if (LocaleCompare(symbol,"quality") == 0)
        return((double) image->quality);
      break;
    }
    case 'R':
    case 'r':
    {
      if (LocaleCompare(symbol,"resolution.x") == 0)
        return(image->x_resolution);
      if (LocaleCompare(symbol,"resolution.y") == 0)
        return(image->y_resolution);
      if (LocaleCompare(symbol,"r") == 0)
        return(QuantumScale*pixel.red);
      break;
    }
    case 'S':
    case 's':
    {
      if (LocaleCompare(symbol,"saturation") == 0)
        {
          double
            hue,
            lightness,
            saturation;

          ConvertRGBToHSL(ClampToQuantum(pixel.red),ClampToQuantum(pixel.green),
            ClampToQuantum(pixel.blue),&hue,&saturation,&lightness);
          return(saturation);
        }
      if (LocaleNCompare(symbol,"skewness",8) == 0)
        return(FxChannelStatistics(fx_info,image,channel,symbol,exception));
      if (LocaleNCompare(symbol,"standard_deviation",18) == 0)
        return(FxChannelStatistics(fx_info,image,channel,symbol,exception));
      break;
    }
    case 'T':
    case 't':
    {
      if (LocaleCompare(symbol,"t") == 0)
        return((double) GetImageIndexInList(fx_info->images));
      break;
    }
    case 'W':
    case 'w':
    {
      if (LocaleCompare(symbol,"w") == 0)
        return((double) image->columns);
      break;
    }
    case 'Y':
    case 'y':
    {
      if (LocaleCompare(symbol,"y") == 0)
        return(QuantumScale*pixel.blue);
      break;
    }
    case 'Z':
    case 'z':
    {
      if (LocaleCompare(symbol,"z") == 0)
        {
          double
            depth;

          depth=(double) GetImageChannelDepth(image,channel,fx_info->exception);
          return(depth);
        }
      break;
    }
    default:
      break;
  }
  value=(const char *) GetValueFromSplayTree(fx_info->symbols,symbol);
  if (value != (const char *) NULL)
    return(StringToDouble(value,(char **) NULL));
  (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
    "UnableToParseExpression","`%s'",symbol);
  return(0.0);
}

static const char *FxOperatorPrecedence(const char *expression,
  ExceptionInfo *exception)
{
  typedef enum
  {
    UndefinedPrecedence,
    NullPrecedence,
    BitwiseComplementPrecedence,
    ExponentPrecedence,
    ExponentialNotationPrecedence,
    MultiplyPrecedence,
    AdditionPrecedence,
    ShiftPrecedence,
    RelationalPrecedence,
    EquivalencyPrecedence,
    BitwiseAndPrecedence,
    BitwiseOrPrecedence,
    LogicalAndPrecedence,
    LogicalOrPrecedence,
    TernaryPrecedence,
    AssignmentPrecedence,
    CommaPrecedence,
    SeparatorPrecedence
  } FxPrecedence;

  FxPrecedence
    precedence,
    target;

  register const char
    *subexpression;

  register int
    c;

  size_t
    level;

  c=(-1);
  level=0;
  subexpression=(const char *) NULL;
  target=NullPrecedence;
  while ((c != '\0') && (*expression != '\0'))
  {
    precedence=UndefinedPrecedence;
    if ((isspace((int) ((unsigned char) *expression)) != 0) || (c == (int) '@'))
      {
        expression++;
        continue;
      }
    switch (*expression)
    {
      case 'A':
      case 'a':
      {
#if defined(MAGICKCORE_HAVE_ACOSH)
        if (IsFxFunction(expression,"acosh",5) != MagickFalse)
          {
            expression+=5;
            break;
          }
#endif
#if defined(MAGICKCORE_HAVE_ASINH)
        if (IsFxFunction(expression,"asinh",5) != MagickFalse)
          {
            expression+=5;
            break;
          }
#endif
#if defined(MAGICKCORE_HAVE_ATANH)
        if (IsFxFunction(expression,"atanh",5) != MagickFalse)
          {
            expression+=5;
            break;
          }
#endif
        if (IsFxFunction(expression,"atan2",5) != MagickFalse)
          {
            expression+=5;
            break;
          }
        break;
      }
      case 'E':
      case 'e':
      {
        if ((isdigit(c) != 0) &&
            ((LocaleNCompare(expression,"E+",2) == 0) ||
             (LocaleNCompare(expression,"E-",2) == 0)))
          {
            expression+=2;  /* scientific notation */
            break;
          }
      }
      case 'J':
      case 'j':
      {
        if ((IsFxFunction(expression,"j0",2) != MagickFalse) ||
            (IsFxFunction(expression,"j1",2) != MagickFalse))
          {
            expression+=2;
            break;
          }
        break;
      }
      case '#':
      {
        while (isxdigit((int) ((unsigned char) *(expression+1))) != 0)
          expression++;
        break;
      }
      default:
        break;
    }
    if ((c == (int) '{') || (c == (int) '['))
      level++;
    else
      if ((c == (int) '}') || (c == (int) ']'))
        level--;
    if (level == 0)
      switch ((unsigned char) *expression)
      {
        case '~':
        case '!':
        {
          precedence=BitwiseComplementPrecedence;
          break;
        }
        case '^':
        case '@':
        {
          precedence=ExponentPrecedence;
          break;
        }
        default:
        {
          if (((c != 0) && ((isdigit(c) != 0) ||
               (strchr(")",c) != (char *) NULL))) &&
              (((islower((int) ((unsigned char) *expression)) != 0) ||
               (strchr("(",(int) ((unsigned char) *expression)) != (char *) NULL)) ||
               ((isdigit(c) == 0) &&
                (isdigit((int) ((unsigned char) *expression)) != 0))) &&
              (strchr("xy",(int) ((unsigned char) *expression)) == (char *) NULL))
            precedence=MultiplyPrecedence;
          break;
        }
        case '*':
        case '/':
        case '%':
        {
          precedence=MultiplyPrecedence;
          break;
        }
        case '+':
        case '-':
        {
          if ((strchr("(+-/*%:&^|<>~,",c) == (char *) NULL) ||
              (isalpha(c) != 0))
            precedence=AdditionPrecedence;
          break;
        }
        case LeftShiftOperator:
        case RightShiftOperator:
        {
          precedence=ShiftPrecedence;
          break;
        }
        case '<':
        case LessThanEqualOperator:
        case GreaterThanEqualOperator:
        case '>':
        {
          precedence=RelationalPrecedence;
          break;
        }
        case EqualOperator:
        case NotEqualOperator:
        {
          precedence=EquivalencyPrecedence;
          break;
        }
        case '&':
        {
          precedence=BitwiseAndPrecedence;
          break;
        }
        case '|':
        {
          precedence=BitwiseOrPrecedence;
          break;
        }
        case LogicalAndOperator:
        {
          precedence=LogicalAndPrecedence;
          break;
        }
        case LogicalOrOperator:
        {
          precedence=LogicalOrPrecedence;
          break;
        }
        case ExponentialNotation:
        {
          precedence=ExponentialNotationPrecedence;
          break;
        }
        case ':':
        case '?':
        {
          precedence=TernaryPrecedence;
          break;
        }
        case '=':
        {
          precedence=AssignmentPrecedence;
          break;
        }
        case ',':
        {
          precedence=CommaPrecedence;
          break;
        }
        case ';':
        {
          precedence=SeparatorPrecedence;
          break;
        }
      }
    if ((precedence == BitwiseComplementPrecedence) ||
        (precedence == TernaryPrecedence) ||
        (precedence == AssignmentPrecedence))
      {
        if (precedence > target)
          {
            /*
              Right-to-left associativity.
            */
            target=precedence;
            subexpression=expression;
          }
      }
    else
      if (precedence >= target)
        {
          /*
            Left-to-right associativity.
          */
          target=precedence;
          subexpression=expression;
        }
    if (strchr("(",(int) *expression) != (char *) NULL)
      expression=FxSubexpression(expression,exception);
    c=(int) (*expression++);
  }
  return(subexpression);
}

static double FxEvaluateSubexpression(FxInfo *fx_info,const ChannelType channel,
  const ssize_t x,const ssize_t y,const char *expression,const size_t depth,
  double *beta,ExceptionInfo *exception)
{
#define FxMaxParenthesisDepth  58
#define FxMaxSubexpressionDepth  200
#define FxReturn(value) \
{ \
  subexpression=DestroyString(subexpression); \
  return(value); \
}

  char
    *q,
    *subexpression;

  double
    alpha,
    gamma;

  register const char
    *p;

  *beta=0.0;
  subexpression=AcquireString(expression);
  *subexpression='\0';
  if (depth > FxMaxSubexpressionDepth)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
        "UnableToParseExpression","`%s'",expression);
      FxReturn(0.0);
    }
  if (exception->severity >= ErrorException)
    FxReturn(0.0);
  while (isspace((int) ((unsigned char) *expression)) != 0)
    expression++;
  if (*expression == '\0')
    FxReturn(0.0);
  p=FxOperatorPrecedence(expression,exception);
  if (p != (const char *) NULL)
    {
      (void) CopyMagickString(subexpression,expression,(size_t)
        (p-expression+1));
      alpha=FxEvaluateSubexpression(fx_info,channel,x,y,subexpression,depth+1,
        beta,exception);
      switch ((unsigned char) *p)
      {
        case '~':
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          *beta=(double) (~(size_t) *beta);
          FxReturn(*beta);
        }
        case '!':
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(*beta == 0.0 ? 1.0 : 0.0);
        }
        case '^':
        {
          *beta=pow(alpha,FxEvaluateSubexpression(fx_info,channel,x,y,++p,
            depth+1,beta,exception));
          FxReturn(*beta);
        }
        case '*':
        case ExponentialNotation:
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(alpha*(*beta));
        }
        case '/':
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(PerceptibleReciprocal(*beta)*alpha);
        }
        case '%':
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          *beta=fabs(floor((*beta)+0.5));
          FxReturn(fmod(alpha,(double) *beta));
        }
        case '+':
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(alpha+(*beta));
        }
        case '-':
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(alpha-(*beta));
        }
        case LeftShiftOperator:
        {
          gamma=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          if ((size_t) (gamma+0.5) > (8*sizeof(size_t)))
            {
              (void) ThrowMagickException(exception,GetMagickModule(),
                OptionError,"ShiftCountOverflow","`%s'",subexpression);
              FxReturn(0.0);
            }
          *beta=(double) ((size_t) (alpha+0.5) << (size_t) (gamma+0.5));
          FxReturn(*beta);
        }
        case RightShiftOperator:
        {
          gamma=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          if ((size_t) (gamma+0.5) > (8*sizeof(size_t)))
            {
              (void) ThrowMagickException(exception,GetMagickModule(),
                OptionError,"ShiftCountOverflow","`%s'",subexpression);
              FxReturn(0.0);
            }
          *beta=(double) ((size_t) (alpha+0.5) >> (size_t) (gamma+0.5));
          FxReturn(*beta);
        }
        case '<':
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(alpha < *beta ? 1.0 : 0.0);
        }
        case LessThanEqualOperator:
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(alpha <= *beta ? 1.0 : 0.0);
        }
        case '>':
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(alpha > *beta ? 1.0 : 0.0);
        }
        case GreaterThanEqualOperator:
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(alpha >= *beta ? 1.0 : 0.0);
        }
        case EqualOperator:
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(fabs(alpha-(*beta)) < MagickEpsilon ? 1.0 : 0.0);
        }
        case NotEqualOperator:
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(fabs(alpha-(*beta)) >= MagickEpsilon ? 1.0 : 0.0);
        }
        case '&':
        {
          gamma=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          *beta=(double) ((size_t) (alpha+0.5) & (size_t) (gamma+0.5));
          FxReturn(*beta);
        }
        case '|':
        {
          gamma=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          *beta=(double) ((size_t) (alpha+0.5) | (size_t) (gamma+0.5));
          FxReturn(*beta);
        }
        case LogicalAndOperator:
        {
          p++;
          if (alpha <= 0.0)
            {
              *beta=0.0;
              FxReturn(*beta);
            }
          gamma=FxEvaluateSubexpression(fx_info,channel,x,y,p,depth+1,beta,
            exception);
          *beta=(gamma > 0.0) ? 1.0 : 0.0;
          FxReturn(*beta);
        }
        case LogicalOrOperator:
        {
          p++;
          if (alpha > 0.0)
            {
             *beta=1.0;
             FxReturn(*beta);
            }
          gamma=FxEvaluateSubexpression(fx_info,channel,x,y,p,depth+1,beta,
            exception);
          *beta=(gamma > 0.0) ? 1.0 : 0.0;
          FxReturn(*beta);
        }
        case '?':
        {
          double
            gamma;

          (void) CopyMagickString(subexpression,++p,MaxTextExtent);
          q=subexpression;
          p=StringToken(":",&q);
          if (q == (char *) NULL)
            {
              (void) ThrowMagickException(exception,GetMagickModule(),
                OptionError,"UnableToParseExpression","`%s'",subexpression);
              FxReturn(0.0);
            }
          if (fabs(alpha) >= MagickEpsilon)
            gamma=FxEvaluateSubexpression(fx_info,channel,x,y,p,depth+1,beta,
              exception);
          else
            gamma=FxEvaluateSubexpression(fx_info,channel,x,y,q,depth+1,beta,
              exception);
          FxReturn(gamma);
        }
        case '=':
        {
          char
            numeric[MaxTextExtent];

          q=subexpression;
          while (isalpha((int) ((unsigned char) *q)) != 0)
            q++;
          if (*q != '\0')
            {
              (void) ThrowMagickException(exception,GetMagickModule(),
                OptionError,"UnableToParseExpression","`%s'",subexpression);
              FxReturn(0.0);
            }
          ClearMagickException(exception);
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          (void) FormatLocaleString(numeric,MaxTextExtent,"%.20g",(double)
            *beta);
          (void) DeleteNodeFromSplayTree(fx_info->symbols,subexpression);
          (void) AddValueToSplayTree(fx_info->symbols,ConstantString(
            subexpression),ConstantString(numeric));
          FxReturn(*beta);
        }
        case ',':
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(alpha);
        }
        case ';':
        {
          *beta=FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,beta,
            exception);
          FxReturn(*beta);
        }
        default:
        {
          gamma=alpha*FxEvaluateSubexpression(fx_info,channel,x,y,++p,depth+1,
            beta,exception);
          FxReturn(gamma);
        }
      }
    }
  if (strchr("(",(int) *expression) != (char *) NULL)
    {
      if (depth >= FxMaxParenthesisDepth)
        (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
          "ParenthesisNestedTooDeeply","`%s'",expression);
      (void) CopyMagickString(subexpression,expression+1,MaxTextExtent);
      if (strlen(subexpression) != 0)
        subexpression[strlen(subexpression)-1]='\0';
      gamma=FxEvaluateSubexpression(fx_info,channel,x,y,subexpression,depth+1,
        beta,exception);
      FxReturn(gamma);
    }
  switch (*expression)
  {
    case '+':
    {
      gamma=FxEvaluateSubexpression(fx_info,channel,x,y,expression+1,depth+1,
        beta,exception);
      FxReturn(1.0*gamma);
    }
    case '-':
    {
      gamma=FxEvaluateSubexpression(fx_info,channel,x,y,expression+1,depth+1,
        beta,exception);
      FxReturn(-1.0*gamma);
    }
    case '~':
    {
      gamma=FxEvaluateSubexpression(fx_info,channel,x,y,expression+1,depth+1,
        beta,exception);
      FxReturn((double) (~(size_t) (gamma+0.5)));
    }
    case 'A':
    case 'a':
    {
      if (IsFxFunction(expression,"abs",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(fabs(alpha));
        }
#if defined(MAGICKCORE_HAVE_ACOSH)
      if (IsFxFunction(expression,"acosh",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          FxReturn(acosh(alpha));
        }
#endif
      if (IsFxFunction(expression,"acos",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          FxReturn(acos(alpha));
        }
#if defined(MAGICKCORE_HAVE_J1)
      if (IsFxFunction(expression,"airy",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          if (alpha == 0.0)
            FxReturn(1.0);
          gamma=2.0*j1((MagickPI*alpha))/(MagickPI*alpha);
          FxReturn(gamma*gamma);
        }
#endif
#if defined(MAGICKCORE_HAVE_ASINH)
      if (IsFxFunction(expression,"asinh",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          FxReturn(asinh(alpha));
        }
#endif
      if (IsFxFunction(expression,"asin",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          FxReturn(asin(alpha));
        }
      if (IsFxFunction(expression,"alt",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(((ssize_t) alpha) & 0x01 ? -1.0 : 1.0);
        }
      if (IsFxFunction(expression,"atan2",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          FxReturn(atan2(alpha,*beta));
        }
#if defined(MAGICKCORE_HAVE_ATANH)
      if (IsFxFunction(expression,"atanh",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          FxReturn(atanh(alpha));
        }
#endif
      if (IsFxFunction(expression,"atan",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          FxReturn(atan(alpha));
        }
      if (LocaleCompare(expression,"a") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'B':
    case 'b':
    {
      if (LocaleCompare(expression,"b") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'C':
    case 'c':
    {
      if (IsFxFunction(expression,"ceil",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          FxReturn(ceil(alpha));
        }
      if (IsFxFunction(expression,"clamp",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          if (alpha < 0.0)
            FxReturn(0.0);
          if (alpha > 1.0)
            FxReturn(1.0);
          FxReturn(alpha);
        }
      if (IsFxFunction(expression,"cosh",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          FxReturn(cosh(alpha));
        }
      if (IsFxFunction(expression,"cos",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(cos(alpha));
        }
      if (LocaleCompare(expression,"c") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'D':
    case 'd':
    {
      if (IsFxFunction(expression,"debug",5) != MagickFalse)
        {
          const char
            *type;

          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          if (fx_info->images->colorspace == CMYKColorspace)
            switch (channel)
            {
              case CyanChannel: type="cyan"; break;
              case MagentaChannel: type="magenta"; break;
              case YellowChannel: type="yellow"; break;
              case OpacityChannel: type="opacity"; break;
              case BlackChannel: type="black"; break;
              default: type="unknown"; break;
            }
          else
            switch (channel)
            {
              case RedChannel: type="red"; break;
              case GreenChannel: type="green"; break;
              case BlueChannel: type="blue"; break;
              case OpacityChannel: type="opacity"; break;
              default: type="unknown"; break;
            }
          *subexpression='\0';
          if (strlen(subexpression) > 1)
            (void) CopyMagickString(subexpression,expression+6,MaxTextExtent);
          if (strlen(subexpression) > 1)
            subexpression[strlen(subexpression)-1]='\0';
          if (fx_info->file != (FILE *) NULL)
            (void) FormatLocaleFile(fx_info->file,
              "%s[%.20g,%.20g].%s: %s=%.*g\n",fx_info->images->filename,
              (double) x,(double) y,type,subexpression,GetMagickPrecision(),
              (double) alpha);
          FxReturn(0.0);
        }
      if (IsFxFunction(expression,"drc",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn((alpha/(*beta*(alpha-1.0)+1.0)));
        }
      break;
    }
    case 'E':
    case 'e':
    {
      if (LocaleCompare(expression,"epsilon") == 0)
        FxReturn(MagickEpsilon);
#if defined(MAGICKCORE_HAVE_ERF)
      if (LocaleNCompare(expression,"erp",3) == 0)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(erf(alpha));
        }
#endif
      if (IsFxFunction(expression,"exp",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(exp(alpha));
        }
      if (LocaleCompare(expression,"e") != MagickFalse)
        FxReturn(2.7182818284590452354);
      break;
    }
    case 'F':
    case 'f':
    {
      if (IsFxFunction(expression,"floor",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          FxReturn(floor(alpha));
        }
      break;
    }
    case 'G':
    case 'g':
    {
      if (IsFxFunction(expression,"gauss",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          gamma=exp((-alpha*alpha/2.0))/sqrt(2.0*MagickPI);
          FxReturn(gamma);
        }
      if (IsFxFunction(expression,"gcd",3) != MagickFalse)
        {
          MagickOffsetType
            gcd;

          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          gcd=FxGCD((MagickOffsetType) (alpha+0.5),(MagickOffsetType)
            (*beta+0.5));
          FxReturn((double) gcd);
        }
      if (LocaleCompare(expression,"g") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'H':
    case 'h':
    {
      if (LocaleCompare(expression,"h") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      if (LocaleCompare(expression,"hue") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      if (IsFxFunction(expression,"hypot",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          FxReturn(hypot(alpha,*beta));
        }
      break;
    }
    case 'K':
    case 'k':
    {
      if (LocaleCompare(expression,"k") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'I':
    case 'i':
    {
      if (LocaleCompare(expression,"intensity") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      if (IsFxFunction(expression,"int",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(floor(alpha));
        }
      if (IsFxFunction(expression,"isnan",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          FxReturn((double) !!IsNaN(alpha));
        }
      if (LocaleCompare(expression,"i") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'J':
    case 'j':
    {
      if (LocaleCompare(expression,"j") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
#if defined(MAGICKCORE_HAVE_J0)
      if (IsFxFunction(expression,"j0",2) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+2,
            depth+1,beta,exception);
          FxReturn(j0(alpha));
        }
#endif
#if defined(MAGICKCORE_HAVE_J1)
      if (IsFxFunction(expression,"j1",2) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+2,
            depth+1,beta,exception);
          FxReturn(j1(alpha));
        }
#endif
#if defined(MAGICKCORE_HAVE_J1)
      if (IsFxFunction(expression,"jinc",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          if (alpha == 0.0)
            FxReturn(1.0);
          gamma=(2.0*j1((MagickPI*alpha))/(MagickPI*alpha));
          FxReturn(gamma);
        }
#endif
      break;
    }
    case 'L':
    case 'l':
    {
      if (IsFxFunction(expression,"ln",2) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+2,
            depth+1,beta,exception);
          FxReturn(log(alpha));
        }
      if (IsFxFunction(expression,"logtwo",6) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+6,
            depth+1,beta,exception);
          FxReturn(log10(alpha)/log10(2.0));
        }
      if (IsFxFunction(expression,"log",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(log10(alpha));
        }
      if (LocaleCompare(expression,"lightness") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'M':
    case 'm':
    {
      if (LocaleCompare(expression,"MaxRGB") == 0)
        FxReturn((double) QuantumRange);
      if (LocaleNCompare(expression,"maxima",6) == 0)
        break;
      if (IsFxFunction(expression,"max",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(alpha > *beta ? alpha : *beta);
        }
      if (LocaleNCompare(expression,"minima",6) == 0)
        break;
      if (IsFxFunction(expression,"min",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(alpha < *beta ? alpha : *beta);
        }
      if (IsFxFunction(expression,"mod",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          gamma=alpha-floor((alpha*PerceptibleReciprocal(*beta)))*(*beta);
          FxReturn(gamma);
        }
      if (LocaleCompare(expression,"m") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'N':
    case 'n':
    {
      if (IsFxFunction(expression,"not",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn((double) (alpha < MagickEpsilon));
        }
      if (LocaleCompare(expression,"n") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'O':
    case 'o':
    {
      if (LocaleCompare(expression,"Opaque") == 0)
        FxReturn(1.0);
      if (LocaleCompare(expression,"o") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'P':
    case 'p':
    {
      if (LocaleCompare(expression,"phi") == 0)
        FxReturn(MagickPHI);
      if (LocaleCompare(expression,"pi") == 0)
        FxReturn(MagickPI);
      if (IsFxFunction(expression,"pow",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(pow(alpha,*beta));
        }
      if (LocaleCompare(expression,"p") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'Q':
    case 'q':
    {
      if (LocaleCompare(expression,"QuantumRange") == 0)
        FxReturn((double) QuantumRange);
      if (LocaleCompare(expression,"QuantumScale") == 0)
        FxReturn(QuantumScale);
      break;
    }
    case 'R':
    case 'r':
    {
      if (IsFxFunction(expression,"rand",4) != MagickFalse)
        {
          double
            alpha;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
          #pragma omp critical (MagickCore_FxEvaluateSubexpression)
#endif
          alpha=GetPseudoRandomValue(fx_info->random_info);
          FxReturn(alpha);
        }
      if (IsFxFunction(expression,"round",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          FxReturn(floor(alpha+0.5));
        }
      if (LocaleCompare(expression,"r") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'S':
    case 's':
    {
      if (LocaleCompare(expression,"saturation") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      if (IsFxFunction(expression,"sign",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          FxReturn(alpha < 0.0 ? -1.0 : 1.0);
        }
      if (IsFxFunction(expression,"sinc",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          if (alpha == 0)
            FxReturn(1.0);
          gamma=(sin((MagickPI*alpha))/(MagickPI*alpha));
          FxReturn(gamma);
        }
      if (IsFxFunction(expression,"sinh",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          FxReturn(sinh(alpha));
        }
      if (IsFxFunction(expression,"sin",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(sin(alpha));
        }
      if (IsFxFunction(expression,"sqrt",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          FxReturn(sqrt(alpha));
        }
      if (IsFxFunction(expression,"squish",6) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+6,
            depth+1,beta,exception);
          FxReturn((1.0/(1.0+exp(-alpha))));
        }
      if (LocaleCompare(expression,"s") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'T':
    case 't':
    {
      if (IsFxFunction(expression,"tanh",4) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+4,
            depth+1,beta,exception);
          FxReturn(tanh(alpha));
        }
      if (IsFxFunction(expression,"tan",3) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+3,
            depth+1,beta,exception);
          FxReturn(tan(alpha));
        }
      if (LocaleCompare(expression,"Transparent") == 0)
        FxReturn(0.0);
      if (IsFxFunction(expression,"trunc",5) != MagickFalse)
        {
          alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
            depth+1,beta,exception);
          if (alpha >= 0.0)
            FxReturn(floor(alpha));
          FxReturn(ceil(alpha));
        }
      if (LocaleCompare(expression,"t") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'U':
    case 'u':
    {
      if (LocaleCompare(expression,"u") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'V':
    case 'v':
    {
      if (LocaleCompare(expression,"v") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'W':
    case 'w':
    {
      if (LocaleNCompare(expression,"while(",6) == 0)
        {
          do
          {
            alpha=FxEvaluateSubexpression(fx_info,channel,x,y,expression+5,
              depth+1,beta,exception);
          } while (fabs(alpha) >= MagickEpsilon);
          FxReturn(*beta);
        }
      if (LocaleCompare(expression,"w") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'Y':
    case 'y':
    {
      if (LocaleCompare(expression,"y") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    case 'Z':
    case 'z':
    {
      if (LocaleCompare(expression,"z") == 0)
        FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
      break;
    }
    default:
      break;
  }
  q=(char *) expression;
  alpha=InterpretSiPrefixValue(expression,&q);
  if (q == expression)
    FxReturn(FxGetSymbol(fx_info,channel,x,y,expression,depth+1,exception));
  FxReturn(alpha);
}

MagickExport MagickBooleanType FxEvaluateExpression(FxInfo *fx_info,
  double *alpha,ExceptionInfo *exception)
{
  MagickBooleanType
    status;

  status=FxEvaluateChannelExpression(fx_info,GrayChannel,0,0,alpha,exception);
  return(status);
}

MagickExport MagickBooleanType FxPreprocessExpression(FxInfo *fx_info,
  double *alpha,ExceptionInfo *exception)
{
  FILE
    *file;

  MagickBooleanType
    status;

  file=fx_info->file;
  fx_info->file=(FILE *) NULL;
  status=FxEvaluateChannelExpression(fx_info,GrayChannel,0,0,alpha,exception);
  fx_info->file=file;
  return(status);
}

MagickExport MagickBooleanType FxEvaluateChannelExpression(FxInfo *fx_info,
  const ChannelType channel,const ssize_t x,const ssize_t y,double *alpha,
  ExceptionInfo *exception)
{
  double
    beta;

  beta=0.0;
  *alpha=FxEvaluateSubexpression(fx_info,channel,x,y,fx_info->expression,0,
    &beta,exception);
  return(exception->severity == OptionError ? MagickFalse : MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     F x I m a g e                                                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  FxImage() applies a mathematical expression to the specified image.
%
%  The format of the FxImage method is:
%
%      Image *FxImage(const Image *image,const char *expression,
%        ExceptionInfo *exception)
%      Image *FxImageChannel(const Image *image,const ChannelType channel,
%        const char *expression,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o channel: the channel.
%
%    o expression: A mathematical expression.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static FxInfo **DestroyFxThreadSet(FxInfo **fx_info)
{
  register ssize_t
    i;

  assert(fx_info != (FxInfo **) NULL);
  for (i=0; i < (ssize_t) GetMagickResourceLimit(ThreadResource); i++)
    if (fx_info[i] != (FxInfo *) NULL)
      fx_info[i]=DestroyFxInfo(fx_info[i]);
  fx_info=(FxInfo **) RelinquishMagickMemory(fx_info);
  return(fx_info);
}

static FxInfo **AcquireFxThreadSet(const Image *image,const char *expression,
  ExceptionInfo *exception)
{
  char
    *fx_expression;

  double
    alpha;

  FxInfo
    **fx_info;

  register ssize_t
    i;

  size_t
    number_threads;

  number_threads=(size_t) GetMagickResourceLimit(ThreadResource);
  fx_info=(FxInfo **) AcquireQuantumMemory(number_threads,sizeof(*fx_info));
  if (fx_info == (FxInfo **) NULL)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),
        ResourceLimitError,"MemoryAllocationFailed","`%s'",image->filename);
      return((FxInfo **) NULL);
    }
  (void) memset(fx_info,0,number_threads*sizeof(*fx_info));
  if (*expression != '@')
    fx_expression=ConstantString(expression);
  else
    fx_expression=FileToString(expression+1,~0UL,exception);
  for (i=0; i < (ssize_t) number_threads; i++)
  {
    MagickBooleanType
      status;

    fx_info[i]=AcquireFxInfo(image,fx_expression);
    if (fx_info[i] == (FxInfo *) NULL)
      break;
    status=FxPreprocessExpression(fx_info[i],&alpha,exception);
    if (status == MagickFalse)
      break;
  }
  fx_expression=DestroyString(fx_expression);
  if (i < (ssize_t) number_threads)
    fx_info=DestroyFxThreadSet(fx_info);
  return(fx_info);
}

MagickExport Image *FxImage(const Image *image,const char *expression,
  ExceptionInfo *exception)
{
  Image
    *fx_image;

  fx_image=FxImageChannel(image,GrayChannel,expression,exception);
  return(fx_image);
}

MagickExport Image *FxImageChannel(const Image *image,const ChannelType channel,
  const char *expression,ExceptionInfo *exception)
{
#define FxImageTag  "Fx/Image"

  CacheView
    *fx_view;

  FxInfo
    **magick_restrict fx_info;

  Image
    *fx_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  ssize_t
    y;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  if (expression == (const char *) NULL)
    return(CloneImage(image,0,0,MagickTrue,exception));
  fx_info=AcquireFxThreadSet(image,expression,exception);
  if (fx_info == (FxInfo **) NULL)
    return((Image *) NULL);
  fx_image=CloneImage(image,0,0,MagickTrue,exception);
  if (fx_image == (Image *) NULL)
    {
      fx_info=DestroyFxThreadSet(fx_info);
      return((Image *) NULL);
    }
  if (SetImageStorageClass(fx_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&fx_image->exception);
      fx_info=DestroyFxThreadSet(fx_info);
      fx_image=DestroyImage(fx_image);
      return((Image *) NULL);
    }
  /*
    Fx image.
  */
  status=MagickTrue;
  progress=0;
  fx_view=AcquireAuthenticCacheView(fx_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,fx_image,fx_image->rows,1)
#endif
  for (y=0; y < (ssize_t) fx_image->rows; y++)
  {
    const int
      id = GetOpenMPThreadId();

    double
      alpha;

    register IndexPacket
      *magick_restrict fx_indexes;

    register ssize_t
      x;

    register PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    q=GetCacheViewAuthenticPixels(fx_view,0,y,fx_image->columns,1,exception);
    if (q == (PixelPacket *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    fx_indexes=GetCacheViewAuthenticIndexQueue(fx_view);
    alpha=0.0;
    for (x=0; x < (ssize_t) fx_image->columns; x++)
    {
      if ((channel & RedChannel) != 0)
        {
          (void) FxEvaluateChannelExpression(fx_info[id],RedChannel,x,y,
            &alpha,exception);
          SetPixelRed(q,ClampToQuantum((MagickRealType) QuantumRange*alpha));
        }
      if ((channel & GreenChannel) != 0)
        {
          (void) FxEvaluateChannelExpression(fx_info[id],GreenChannel,x,y,
            &alpha,exception);
          SetPixelGreen(q,ClampToQuantum((MagickRealType) QuantumRange*alpha));
        }
      if ((channel & BlueChannel) != 0)
        {
          (void) FxEvaluateChannelExpression(fx_info[id],BlueChannel,x,y,
            &alpha,exception);
          SetPixelBlue(q,ClampToQuantum((MagickRealType) QuantumRange*alpha));
        }
      if ((channel & OpacityChannel) != 0)
        {
          (void) FxEvaluateChannelExpression(fx_info[id],OpacityChannel,x,y,
            &alpha,exception);
          if (image->matte == MagickFalse)
            SetPixelOpacity(q,ClampToQuantum((MagickRealType) QuantumRange*
              alpha));
          else
            SetPixelOpacity(q,ClampToQuantum((MagickRealType) (QuantumRange-
              QuantumRange*alpha)));
        }
      if (((channel & IndexChannel) != 0) &&
          (fx_image->colorspace == CMYKColorspace))
        {
          (void) FxEvaluateChannelExpression(fx_info[id],IndexChannel,x,y,
            &alpha,exception);
          SetPixelIndex(fx_indexes+x,ClampToQuantum((MagickRealType)
            QuantumRange*alpha));
        }
      q++;
    }
    if (SyncCacheViewAuthenticPixels(fx_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,FxImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  fx_view=DestroyCacheView(fx_view);
  fx_info=DestroyFxThreadSet(fx_info);
  if (status == MagickFalse)
    fx_image=DestroyImage(fx_image);
  return(fx_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     I m p l o d e I m a g e                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ImplodeImage() creates a new image that is a copy of an existing
%  one with the image pixels "implode" by the specified percentage.  It
%  allocates the memory necessary for the new Image structure and returns a
%  pointer to the new image.
%
%  The format of the ImplodeImage method is:
%
%      Image *ImplodeImage(const Image *image,const double amount,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o implode_image: Method ImplodeImage returns a pointer to the image
%      after it is implode.  A null image is returned if there is a memory
%      shortage.
%
%    o image: the image.
%
%    o amount:  Define the extent of the implosion.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *ImplodeImage(const Image *image,const double amount,
  ExceptionInfo *exception)
{
#define ImplodeImageTag  "Implode/Image"

  CacheView
    *image_view,
    *implode_view;

  double
    radius;

  Image
    *implode_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  MagickPixelPacket
    zero;

  PointInfo
    center,
    scale;

  ssize_t
    y;

  /*
    Initialize implode image attributes.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  implode_image=CloneImage(image,0,0,MagickTrue,exception);
  if (implode_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(implode_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&implode_image->exception);
      implode_image=DestroyImage(implode_image);
      return((Image *) NULL);
    }
  if (implode_image->background_color.opacity != OpaqueOpacity)
    implode_image->matte=MagickTrue;
  /*
    Compute scaling factor.
  */
  scale.x=1.0;
  scale.y=1.0;
  center.x=0.5*image->columns;
  center.y=0.5*image->rows;
  radius=center.x;
  if (image->columns > image->rows)
    scale.y=(double) image->columns/(double) image->rows;
  else
    if (image->columns < image->rows)
      {
        scale.x=(double) image->rows/(double) image->columns;
        radius=center.y;
      }
  /*
    Implode image.
  */
  status=MagickTrue;
  progress=0;
  GetMagickPixelPacket(implode_image,&zero);
  image_view=AcquireVirtualCacheView(image,exception);
  implode_view=AcquireAuthenticCacheView(implode_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,implode_image,image->rows,1)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    double
      distance;

    MagickPixelPacket
      pixel;

    PointInfo
      delta;

    register IndexPacket
      *magick_restrict implode_indexes;

    register ssize_t
      x;

    register PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    q=GetCacheViewAuthenticPixels(implode_view,0,y,implode_image->columns,1,
      exception);
    if (q == (PixelPacket *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    implode_indexes=GetCacheViewAuthenticIndexQueue(implode_view);
    delta.y=scale.y*(double) (y-center.y);
    pixel=zero;
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      /*
        Determine if the pixel is within an ellipse.
      */
      delta.x=scale.x*(double) (x-center.x);
      distance=delta.x*delta.x+delta.y*delta.y;
      if (distance < (radius*radius))
        {
          double
            factor;

          /*
            Implode the pixel.
          */
          factor=1.0;
          if (distance > 0.0)
            factor=pow(sin((double) (MagickPI*sqrt((double) distance)/
              radius/2)),-amount);
          status=InterpolateMagickPixelPacket(image,image_view,
            UndefinedInterpolatePixel,(double) (factor*delta.x/scale.x+
            center.x),(double) (factor*delta.y/scale.y+center.y),&pixel,
            exception);
          if (status == MagickFalse)
            break;
          SetPixelPacket(implode_image,&pixel,q,implode_indexes+x);
        }
      q++;
    }
    if (SyncCacheViewAuthenticPixels(implode_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,ImplodeImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  implode_view=DestroyCacheView(implode_view);
  image_view=DestroyCacheView(image_view);
  if (status == MagickFalse)
    implode_image=DestroyImage(implode_image);
  return(implode_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     M o r p h I m a g e s                                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  The MorphImages() method requires a minimum of two images.  The first
%  image is transformed into the second by a number of intervening images
%  as specified by frames.
%
%  The format of the MorphImage method is:
%
%      Image *MorphImages(const Image *image,const size_t number_frames,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o number_frames:  Define the number of in-between image to generate.
%      The more in-between frames, the smoother the morph.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *MorphImages(const Image *image,
  const size_t number_frames,ExceptionInfo *exception)
{
#define MorphImageTag  "Morph/Image"

  double
    alpha,
    beta;

  Image
    *morph_image,
    *morph_images;

  MagickBooleanType
    status;

  MagickOffsetType
    scene;

  register const Image
    *next;

  register ssize_t
    i;

  ssize_t
    y;

  /*
    Clone first frame in sequence.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  morph_images=CloneImage(image,0,0,MagickTrue,exception);
  if (morph_images == (Image *) NULL)
    return((Image *) NULL);
  if (GetNextImageInList(image) == (Image *) NULL)
    {
      /*
        Morph single image.
      */
      for (i=1; i < (ssize_t) number_frames; i++)
      {
        morph_image=CloneImage(image,0,0,MagickTrue,exception);
        if (morph_image == (Image *) NULL)
          {
            morph_images=DestroyImageList(morph_images);
            return((Image *) NULL);
          }
        AppendImageToList(&morph_images,morph_image);
        if (image->progress_monitor != (MagickProgressMonitor) NULL)
          {
            MagickBooleanType
              proceed;

            proceed=SetImageProgress(image,MorphImageTag,(MagickOffsetType) i,
              number_frames);
            if (proceed == MagickFalse)
              status=MagickFalse;
          }
      }
      return(GetFirstImageInList(morph_images));
    }
  /*
    Morph image sequence.
  */
  status=MagickTrue;
  scene=0;
  next=image;
  for ( ; GetNextImageInList(next) != (Image *) NULL; next=GetNextImageInList(next))
  {
    for (i=0; i < (ssize_t) number_frames; i++)
    {
      CacheView
        *image_view,
        *morph_view;

      beta=(double) (i+1.0)/(double) (number_frames+1.0);
      alpha=1.0-beta;
      morph_image=ResizeImage(next,(size_t) (alpha*next->columns+beta*
        GetNextImageInList(next)->columns+0.5),(size_t) (alpha*
        next->rows+beta*GetNextImageInList(next)->rows+0.5),
        next->filter,next->blur,exception);
      if (morph_image == (Image *) NULL)
        {
          morph_images=DestroyImageList(morph_images);
          return((Image *) NULL);
        }
      if (SetImageStorageClass(morph_image,DirectClass) == MagickFalse)
        {
          InheritException(exception,&morph_image->exception);
          morph_image=DestroyImage(morph_image);
          return((Image *) NULL);
        }
      AppendImageToList(&morph_images,morph_image);
      morph_images=GetLastImageInList(morph_images);
      morph_image=ResizeImage(GetNextImageInList(next),morph_images->columns,
        morph_images->rows,GetNextImageInList(next)->filter,
        GetNextImageInList(next)->blur,exception);
      if (morph_image == (Image *) NULL)
        {
          morph_images=DestroyImageList(morph_images);
          return((Image *) NULL);
        }
      image_view=AcquireVirtualCacheView(morph_image,exception);
      morph_view=AcquireAuthenticCacheView(morph_images,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
      #pragma omp parallel for schedule(static) shared(status) \
        magick_number_threads(morph_image,morph_image,morph_image->rows,1)
#endif
      for (y=0; y < (ssize_t) morph_images->rows; y++)
      {
        MagickBooleanType
          sync;

        register const PixelPacket
          *magick_restrict p;

        register ssize_t
          x;

        register PixelPacket
          *magick_restrict q;

        if (status == MagickFalse)
          continue;
        p=GetCacheViewVirtualPixels(image_view,0,y,morph_image->columns,1,
          exception);
        q=GetCacheViewAuthenticPixels(morph_view,0,y,morph_images->columns,1,
          exception);
        if ((p == (const PixelPacket *) NULL) || (q == (PixelPacket *) NULL))
          {
            status=MagickFalse;
            continue;
          }
        for (x=0; x < (ssize_t) morph_images->columns; x++)
        {
          SetPixelRed(q,ClampToQuantum(alpha*
            GetPixelRed(q)+beta*GetPixelRed(p)));
          SetPixelGreen(q,ClampToQuantum(alpha*
            GetPixelGreen(q)+beta*GetPixelGreen(p)));
          SetPixelBlue(q,ClampToQuantum(alpha*
            GetPixelBlue(q)+beta*GetPixelBlue(p)));
          SetPixelOpacity(q,ClampToQuantum(alpha*
            GetPixelOpacity(q)+beta*GetPixelOpacity(p)));
          p++;
          q++;
        }
        sync=SyncCacheViewAuthenticPixels(morph_view,exception);
        if (sync == MagickFalse)
          status=MagickFalse;
      }
      morph_view=DestroyCacheView(morph_view);
      image_view=DestroyCacheView(image_view);
      morph_image=DestroyImage(morph_image);
    }
    if (i < (ssize_t) number_frames)
      break;
    /*
      Clone last frame in sequence.
    */
    morph_image=CloneImage(GetNextImageInList(next),0,0,MagickTrue,exception);
    if (morph_image == (Image *) NULL)
      {
        morph_images=DestroyImageList(morph_images);
        return((Image *) NULL);
      }
    AppendImageToList(&morph_images,morph_image);
    morph_images=GetLastImageInList(morph_images);
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

        proceed=SetImageProgress(image,MorphImageTag,scene,
          GetImageListLength(image));
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
    scene++;
  }
  if (GetNextImageInList(next) != (Image *) NULL)
    {
      morph_images=DestroyImageList(morph_images);
      return((Image *) NULL);
    }
  return(GetFirstImageInList(morph_images));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     P l a s m a I m a g e                                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  PlasmaImage() initializes an image with plasma fractal values.  The image
%  must be initialized with a base color and the random number generator
%  seeded before this method is called.
%
%  The format of the PlasmaImage method is:
%
%      MagickBooleanType PlasmaImage(Image *image,const SegmentInfo *segment,
%        size_t attenuate,size_t depth)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o segment:   Define the region to apply plasma fractals values.
%
%    o attenuate: Define the plasma attenuation factor.
%
%    o depth: Limit the plasma recursion depth.
%
*/

static inline Quantum PlasmaPixel(RandomInfo *random_info,
  const MagickRealType pixel,const double noise)
{
  Quantum
    plasma;

  plasma=ClampToQuantum(pixel+noise*GetPseudoRandomValue(random_info)-
    noise/2.0);
  if (plasma <= 0)
    return((Quantum) 0);
  if (plasma >= QuantumRange)
    return(QuantumRange);
  return(plasma);
}

MagickExport MagickBooleanType PlasmaImageProxy(Image *image,
  CacheView *image_view,CacheView *u_view,CacheView *v_view,
  RandomInfo *random_info,const SegmentInfo *segment,size_t attenuate,
  size_t depth)
{
  ExceptionInfo
    *exception;

  double
    plasma;

  PixelPacket
    u,
    v;

  ssize_t
    x,
    x_mid,
    y,
    y_mid;

  if ((fabs(segment->x2-segment->x1) <= MagickEpsilon) &&
      (fabs(segment->y2-segment->y1) <= MagickEpsilon))
    return(MagickTrue);
  if (depth != 0)
    {
      MagickBooleanType
        status;

      SegmentInfo
        local_info;

      /*
        Divide the area into quadrants and recurse.
      */
      depth--;
      attenuate++;
      x_mid=(ssize_t) ceil((segment->x1+segment->x2)/2-0.5);
      y_mid=(ssize_t) ceil((segment->y1+segment->y2)/2-0.5);
      local_info=(*segment);
      local_info.x2=(double) x_mid;
      local_info.y2=(double) y_mid;
      (void) PlasmaImageProxy(image,image_view,u_view,v_view,random_info,
        &local_info,attenuate,depth);
      local_info=(*segment);
      local_info.y1=(double) y_mid;
      local_info.x2=(double) x_mid;
      (void) PlasmaImageProxy(image,image_view,u_view,v_view,random_info,
        &local_info,attenuate,depth);
      local_info=(*segment);
      local_info.x1=(double) x_mid;
      local_info.y2=(double) y_mid;
      (void) PlasmaImageProxy(image,image_view,u_view,v_view,random_info,
        &local_info,attenuate,depth);
      local_info=(*segment);
      local_info.x1=(double) x_mid;
      local_info.y1=(double) y_mid;
      status=PlasmaImageProxy(image,image_view,u_view,v_view,random_info,
        &local_info,attenuate,depth);
      return(status);
    }
  x_mid=(ssize_t) ceil((segment->x1+segment->x2)/2-0.5);
  y_mid=(ssize_t) ceil((segment->y1+segment->y2)/2-0.5);
  if ((fabs(segment->x1-x_mid) < MagickEpsilon) &&
      (fabs(segment->x2-x_mid) < MagickEpsilon) &&
      (fabs(segment->y1-y_mid) < MagickEpsilon) &&
      (fabs(segment->y2-y_mid) < MagickEpsilon))
    return(MagickFalse);
  /*
    Average pixels and apply plasma.
  */
  exception=(&image->exception);
  plasma=(double) QuantumRange/(2.0*attenuate);
  if ((fabs(segment->x1-x_mid) > MagickEpsilon) ||
      (fabs(segment->x2-x_mid) > MagickEpsilon))
    {
      register PixelPacket
        *magick_restrict q;

      /*
        Left pixel.
      */
      x=(ssize_t) ceil(segment->x1-0.5);
      (void) GetOneCacheViewVirtualPixel(u_view,x,(ssize_t)
        ceil(segment->y1-0.5),&u,exception);
      (void) GetOneCacheViewVirtualPixel(v_view,x,(ssize_t)
        ceil(segment->y2-0.5),&v,exception);
      q=QueueCacheViewAuthenticPixels(image_view,x,y_mid,1,1,exception);
      if (q == (PixelPacket *) NULL)
        return(MagickTrue);
      SetPixelRed(q,PlasmaPixel(random_info,(MagickRealType) (u.red+v.red)/2.0,
        plasma));
      SetPixelGreen(q,PlasmaPixel(random_info,(MagickRealType) (u.green+
        v.green)/2.0,plasma));
      SetPixelBlue(q,PlasmaPixel(random_info,(MagickRealType) (u.blue+v.blue)/
        2.0,plasma));
      (void) SyncCacheViewAuthenticPixels(image_view,exception);
      if (fabs(segment->x1-segment->x2) > MagickEpsilon)
        {
          /*
            Right pixel.
          */
          x=(ssize_t) ceil(segment->x2-0.5);
          (void) GetOneCacheViewVirtualPixel(u_view,x,(ssize_t)
            ceil(segment->y1-0.5),&u,exception);
          (void) GetOneCacheViewVirtualPixel(v_view,x,(ssize_t)
            ceil(segment->y2-0.5),&v,exception);
          q=QueueCacheViewAuthenticPixels(image_view,x,y_mid,1,1,exception);
          if (q == (PixelPacket *) NULL)
            return(MagickTrue);
          SetPixelRed(q,PlasmaPixel(random_info,(MagickRealType) (u.red+v.red)/
            2.0,plasma));
          SetPixelGreen(q,PlasmaPixel(random_info,(MagickRealType) (u.green+
            v.green)/2.0,plasma));
          SetPixelBlue(q,PlasmaPixel(random_info,(MagickRealType) (u.blue+
            v.blue)/2.0,plasma));
          (void) SyncCacheViewAuthenticPixels(image_view,exception);
        }
    }
  if ((fabs(segment->y1-y_mid) > MagickEpsilon) ||
      (fabs(segment->y2-y_mid) > MagickEpsilon))
    {
      if ((fabs(segment->x1-x_mid) > MagickEpsilon) ||
          (fabs(segment->y2-y_mid) > MagickEpsilon))
        {
          register PixelPacket
            *magick_restrict q;

          /*
            Bottom pixel.
          */
          y=(ssize_t) ceil(segment->y2-0.5);
          (void) GetOneCacheViewVirtualPixel(u_view,(ssize_t)
            ceil(segment->x1-0.5),y,&u,exception);
          (void) GetOneCacheViewVirtualPixel(v_view,(ssize_t)
            ceil(segment->x2-0.5),y,&v,exception);
          q=QueueCacheViewAuthenticPixels(image_view,x_mid,y,1,1,exception);
          if (q == (PixelPacket *) NULL)
            return(MagickTrue);
          SetPixelRed(q,PlasmaPixel(random_info,(MagickRealType) (u.red+v.red)/
            2.0,plasma));
          SetPixelGreen(q,PlasmaPixel(random_info,(MagickRealType) (u.green+
            v.green)/2.0,plasma));
          SetPixelBlue(q,PlasmaPixel(random_info,(MagickRealType) (u.blue+
            v.blue)/2.0,plasma));
          (void) SyncCacheViewAuthenticPixels(image_view,exception);
        }
      if (fabs(segment->y1-segment->y2) > MagickEpsilon)
        {
          register PixelPacket
            *magick_restrict q;

          /*
            Top pixel.
          */
          y=(ssize_t) ceil(segment->y1-0.5);
          (void) GetOneCacheViewVirtualPixel(u_view,(ssize_t)
            ceil(segment->x1-0.5),y,&u,exception);
          (void) GetOneCacheViewVirtualPixel(v_view,(ssize_t)
            ceil(segment->x2-0.5),y,&v,exception);
          q=QueueCacheViewAuthenticPixels(image_view,x_mid,y,1,1,exception);
          if (q == (PixelPacket *) NULL)
            return(MagickTrue);
          SetPixelRed(q,PlasmaPixel(random_info,(MagickRealType) (u.red+
            v.red)/2.0,plasma));
          SetPixelGreen(q,PlasmaPixel(random_info,(MagickRealType) (u.green+
            v.green)/2.0,plasma));
          SetPixelBlue(q,PlasmaPixel(random_info,(MagickRealType) (u.blue+
            v.blue)/2.0,plasma));
          (void) SyncCacheViewAuthenticPixels(image_view,exception);
        }
    }
  if ((fabs(segment->x1-segment->x2) > MagickEpsilon) ||
      (fabs(segment->y1-segment->y2) > MagickEpsilon))
    {
      register PixelPacket
        *magick_restrict q;

      /*
        Middle pixel.
      */
      x=(ssize_t) ceil(segment->x1-0.5);
      y=(ssize_t) ceil(segment->y1-0.5);
      (void) GetOneCacheViewVirtualPixel(u_view,x,y,&u,exception);
      x=(ssize_t) ceil(segment->x2-0.5);
      y=(ssize_t) ceil(segment->y2-0.5);
      (void) GetOneCacheViewVirtualPixel(v_view,x,y,&v,exception);
      q=QueueCacheViewAuthenticPixels(image_view,x_mid,y_mid,1,1,exception);
      if (q == (PixelPacket *) NULL)
        return(MagickTrue);
      SetPixelRed(q,PlasmaPixel(random_info,(MagickRealType) (u.red+v.red)/2.0,
        plasma));
      SetPixelGreen(q,PlasmaPixel(random_info,(MagickRealType) (u.green+
        v.green)/2.0,plasma));
      SetPixelBlue(q,PlasmaPixel(random_info,(MagickRealType) (u.blue+v.blue)/
        2.0,plasma));
      (void) SyncCacheViewAuthenticPixels(image_view,exception);
    }
  if ((fabs(segment->x2-segment->x1) < 3.0) &&
      (fabs(segment->y2-segment->y1) < 3.0))
    return(MagickTrue);
  return(MagickFalse);
}

MagickExport MagickBooleanType PlasmaImage(Image *image,
  const SegmentInfo *segment,size_t attenuate,size_t depth)
{
  CacheView
    *image_view,
    *u_view,
    *v_view;

  MagickBooleanType
    status;

  RandomInfo
    *random_info;

  assert(image != (Image *) NULL);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  if (SetImageStorageClass(image,DirectClass) == MagickFalse)
    return(MagickFalse);
  image_view=AcquireAuthenticCacheView(image,&image->exception);
  u_view=AcquireVirtualCacheView(image,&image->exception);
  v_view=AcquireVirtualCacheView(image,&image->exception);
  random_info=AcquireRandomInfo();
  status=PlasmaImageProxy(image,image_view,u_view,v_view,random_info,segment,
    attenuate,depth);
  random_info=DestroyRandomInfo(random_info);
  v_view=DestroyCacheView(v_view);
  u_view=DestroyCacheView(u_view);
  image_view=DestroyCacheView(image_view);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   P o l a r o i d I m a g e                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  PolaroidImage() simulates a Polaroid picture.
%
%  The format of the AnnotateImage method is:
%
%      Image *PolaroidImage(const Image *image,const DrawInfo *draw_info,
%        const double angle,ExceptionInfo exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o angle: Apply the effect along this angle.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *PolaroidImage(const Image *image,const DrawInfo *draw_info,
  const double angle,ExceptionInfo *exception)
{
  const char
    *value;

  Image
    *bend_image,
    *caption_image,
    *flop_image,
    *picture_image,
    *polaroid_image,
    *rotate_image,
    *trim_image;

  size_t
    height;

  ssize_t
    quantum;

  /*
    Simulate a Polaroid picture.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  quantum=(ssize_t) MagickMax(MagickMax((double) image->columns,(double)
    image->rows)/25.0,10.0);
  height=image->rows+2*quantum;
  caption_image=(Image *) NULL;
  value=GetImageProperty(image,"Caption");
  if (value != (const char *) NULL)
    {
      char
        *caption;

      /*
        Generate caption image.
      */
      caption_image=CloneImage(image,image->columns,1,MagickTrue,exception);
      if (caption_image == (Image *) NULL)
        return((Image *) NULL);
      caption=InterpretImageProperties((ImageInfo *) NULL,(Image *) image,
        value);
      if (caption != (char *) NULL)
        {
          char
            geometry[MaxTextExtent];

          DrawInfo
            *annotate_info;

          MagickBooleanType
            status;

          ssize_t
            count;

          TypeMetric
            metrics;

          annotate_info=CloneDrawInfo((const ImageInfo *) NULL,draw_info);
          (void) CloneString(&annotate_info->text,caption);
          count=FormatMagickCaption(caption_image,annotate_info,MagickTrue,
            &metrics,&caption);
          status=SetImageExtent(caption_image,image->columns,(size_t)
            ((count+1)*(metrics.ascent-metrics.descent)+0.5));
          if (status == MagickFalse)
            caption_image=DestroyImage(caption_image);
          else
            {
              caption_image->background_color=image->border_color;
              (void) SetImageBackgroundColor(caption_image);
              (void) CloneString(&annotate_info->text,caption);
              (void) FormatLocaleString(geometry,MaxTextExtent,"+0+%.20g",
                metrics.ascent);
              if (annotate_info->gravity == UndefinedGravity)
                (void) CloneString(&annotate_info->geometry,AcquireString(
                  geometry));
              (void) AnnotateImage(caption_image,annotate_info);
              height+=caption_image->rows;
            }
          annotate_info=DestroyDrawInfo(annotate_info);
          caption=DestroyString(caption);
        }
    }
  picture_image=CloneImage(image,image->columns+2*quantum,height,MagickTrue,
    exception);
  if (picture_image == (Image *) NULL)
    {
      if (caption_image != (Image *) NULL)
        caption_image=DestroyImage(caption_image);
      return((Image *) NULL);
    }
  picture_image->background_color=image->border_color;
  (void) SetImageBackgroundColor(picture_image);
  (void) CompositeImage(picture_image,OverCompositeOp,image,quantum,quantum);
  if (caption_image != (Image *) NULL)
    {
      (void) CompositeImage(picture_image,OverCompositeOp,caption_image,
        quantum,(ssize_t) (image->rows+3*quantum/2));
      caption_image=DestroyImage(caption_image);
    }
  (void) QueryColorDatabase("none",&picture_image->background_color,exception);
  (void) SetImageAlphaChannel(picture_image,OpaqueAlphaChannel);
  rotate_image=RotateImage(picture_image,90.0,exception);
  picture_image=DestroyImage(picture_image);
  if (rotate_image == (Image *) NULL)
    return((Image *) NULL);
  picture_image=rotate_image;
  bend_image=WaveImage(picture_image,0.01*picture_image->rows,2.0*
    picture_image->columns,exception);
  picture_image=DestroyImage(picture_image);
  if (bend_image == (Image *) NULL)
    return((Image *) NULL);
  InheritException(&bend_image->exception,exception);
  picture_image=bend_image;
  rotate_image=RotateImage(picture_image,-90.0,exception);
  picture_image=DestroyImage(picture_image);
  if (rotate_image == (Image *) NULL)
    return((Image *) NULL);
  picture_image=rotate_image;
  picture_image->background_color=image->background_color;
  polaroid_image=ShadowImage(picture_image,80.0,2.0,quantum/3,quantum/3,
    exception);
  if (polaroid_image == (Image *) NULL)
    {
      picture_image=DestroyImage(picture_image);
      return(picture_image);
    }
  flop_image=FlopImage(polaroid_image,exception);
  polaroid_image=DestroyImage(polaroid_image);
  if (flop_image == (Image *) NULL)
    {
      picture_image=DestroyImage(picture_image);
      return(picture_image);
    }
  polaroid_image=flop_image;
  (void) CompositeImage(polaroid_image,OverCompositeOp,picture_image,
    (ssize_t) (-0.01*picture_image->columns/2.0),0L);
  picture_image=DestroyImage(picture_image);
  (void) QueryColorDatabase("none",&polaroid_image->background_color,exception);
  rotate_image=RotateImage(polaroid_image,angle,exception);
  polaroid_image=DestroyImage(polaroid_image);
  if (rotate_image == (Image *) NULL)
    return((Image *) NULL);
  polaroid_image=rotate_image;
  trim_image=TrimImage(polaroid_image,exception);
  polaroid_image=DestroyImage(polaroid_image);
  if (trim_image == (Image *) NULL)
    return((Image *) NULL);
  polaroid_image=trim_image;
  return(polaroid_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     S e p i a T o n e I m a g e                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  MagickSepiaToneImage() applies a special effect to the image, similar to the
%  effect achieved in a photo darkroom by sepia toning.  Threshold ranges from
%  0 to QuantumRange and is a measure of the extent of the sepia toning.  A
%  threshold of 80% is a good starting point for a reasonable tone.
%
%  The format of the SepiaToneImage method is:
%
%      Image *SepiaToneImage(const Image *image,const double threshold,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o threshold: the tone threshold.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *SepiaToneImage(const Image *image,const double threshold,
  ExceptionInfo *exception)
{
#define SepiaToneImageTag  "SepiaTone/Image"

  CacheView
    *image_view,
    *sepia_view;

  Image
    *sepia_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  ssize_t
    y;

  /*
    Initialize sepia-toned image attributes.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  sepia_image=CloneImage(image,0,0,MagickTrue,exception);
  if (sepia_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(sepia_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&sepia_image->exception);
      sepia_image=DestroyImage(sepia_image);
      return((Image *) NULL);
    }
  /*
    Tone each row of the image.
  */
  status=MagickTrue;
  progress=0;
  image_view=AcquireVirtualCacheView(image,exception);
  sepia_view=AcquireAuthenticCacheView(sepia_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,sepia_image,image->rows,1)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    register const PixelPacket
      *magick_restrict p;

    register ssize_t
      x;

    register PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    p=GetCacheViewVirtualPixels(image_view,0,y,image->columns,1,exception);
    q=QueueCacheViewAuthenticPixels(sepia_view,0,y,sepia_image->columns,1,
      exception);
    if ((p == (const PixelPacket *) NULL) || (q == (PixelPacket *) NULL))
      {
        status=MagickFalse;
        continue;
      }
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      double
        intensity,
        tone;

      intensity=GetPixelIntensity(image,p);
      tone=intensity > threshold ? (double) QuantumRange : intensity+
        (double) QuantumRange-threshold;
      SetPixelRed(q,ClampToQuantum(tone));
      tone=intensity > (7.0*threshold/6.0) ? (double) QuantumRange :
        intensity+(double) QuantumRange-7.0*threshold/6.0;
      SetPixelGreen(q,ClampToQuantum(tone));
      tone=intensity < (threshold/6.0) ? 0 : intensity-threshold/6.0;
      SetPixelBlue(q,ClampToQuantum(tone));
      tone=threshold/7.0;
      if ((double) GetPixelGreen(q) < tone)
        SetPixelGreen(q,ClampToQuantum(tone));
      if ((double) GetPixelBlue(q) < tone)
        SetPixelBlue(q,ClampToQuantum(tone));
      SetPixelOpacity(q,GetPixelOpacity(p));
      p++;
      q++;
    }
    if (SyncCacheViewAuthenticPixels(sepia_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,SepiaToneImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  sepia_view=DestroyCacheView(sepia_view);
  image_view=DestroyCacheView(image_view);
  (void) NormalizeImage(sepia_image);
  (void) ContrastImage(sepia_image,MagickTrue);
  if (status == MagickFalse)
    sepia_image=DestroyImage(sepia_image);
  return(sepia_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     S h a d o w I m a g e                                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ShadowImage() simulates a shadow from the specified image and returns it.
%
%  The format of the ShadowImage method is:
%
%      Image *ShadowImage(const Image *image,const double opacity,
%        const double sigma,const ssize_t x_offset,const ssize_t y_offset,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o opacity: percentage transparency.
%
%    o sigma: the standard deviation of the Gaussian, in pixels.
%
%    o x_offset: the shadow x-offset.
%
%    o y_offset: the shadow y-offset.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *ShadowImage(const Image *image,const double opacity,
  const double sigma,const ssize_t x_offset,const ssize_t y_offset,
  ExceptionInfo *exception)
{
#define ShadowImageTag  "Shadow/Image"

  CacheView
    *image_view;

  Image
    *border_image,
    *clone_image,
    *shadow_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  RectangleInfo
    border_info;

  ssize_t
    y;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  clone_image=CloneImage(image,0,0,MagickTrue,exception);
  if (clone_image == (Image *) NULL)
    return((Image *) NULL);
  if (IsGrayColorspace(image->colorspace) != MagickFalse)
    (void) SetImageColorspace(clone_image,sRGBColorspace);
  (void) SetImageVirtualPixelMethod(clone_image,EdgeVirtualPixelMethod);
  clone_image->compose=OverCompositeOp;
  border_info.width=(size_t) floor(2.0*sigma+0.5);
  border_info.height=(size_t) floor(2.0*sigma+0.5);
  border_info.x=0;
  border_info.y=0;
  (void) QueryColorDatabase("none",&clone_image->border_color,exception);
  border_image=BorderImage(clone_image,&border_info,exception);
  clone_image=DestroyImage(clone_image);
  if (border_image == (Image *) NULL)
    return((Image *) NULL);
  if (border_image->matte == MagickFalse)
    (void) SetImageAlphaChannel(border_image,OpaqueAlphaChannel);
  /*
    Shadow image.
  */
  status=MagickTrue;
  progress=0;
  image_view=AcquireAuthenticCacheView(border_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(border_image,border_image,border_image->rows,1)
#endif
  for (y=0; y < (ssize_t) border_image->rows; y++)
  {
    register PixelPacket
      *magick_restrict q;

    register ssize_t
      x;

    if (status == MagickFalse)
      continue;
    q=GetCacheViewAuthenticPixels(image_view,0,y,border_image->columns,1,
      exception);
    if (q == (PixelPacket *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    for (x=0; x < (ssize_t) border_image->columns; x++)
    {
      SetPixelRed(q,border_image->background_color.red);
      SetPixelGreen(q,border_image->background_color.green);
      SetPixelBlue(q,border_image->background_color.blue);
      if (border_image->matte == MagickFalse)
        SetPixelOpacity(q,border_image->background_color.opacity);
      else
        SetPixelOpacity(q,ClampToQuantum((double) (QuantumRange-
          GetPixelAlpha(q)*opacity/100.0)));
      q++;
    }
    if (SyncCacheViewAuthenticPixels(image_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,ShadowImageTag,progress,
          border_image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  image_view=DestroyCacheView(image_view);
  shadow_image=BlurImageChannel(border_image,AlphaChannel,0.0,sigma,exception);
  border_image=DestroyImage(border_image);
  if (shadow_image == (Image *) NULL)
    return((Image *) NULL);
  if (shadow_image->page.width == 0)
    shadow_image->page.width=shadow_image->columns;
  if (shadow_image->page.height == 0)
    shadow_image->page.height=shadow_image->rows;
  shadow_image->page.width+=x_offset-(ssize_t) border_info.width;
  shadow_image->page.height+=y_offset-(ssize_t) border_info.height;
  shadow_image->page.x+=x_offset-(ssize_t) border_info.width;
  shadow_image->page.y+=y_offset-(ssize_t) border_info.height;
  return(shadow_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     S k e t c h I m a g e                                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SketchImage() simulates a pencil sketch.  We convolve the image with a
%  Gaussian operator of the given radius and standard deviation (sigma).  For
%  reasonable results, radius should be larger than sigma.  Use a radius of 0
%  and SketchImage() selects a suitable radius for you.  Angle gives the angle
%  of the sketch.
%
%  The format of the SketchImage method is:
%
%    Image *SketchImage(const Image *image,const double radius,
%      const double sigma,const double angle,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o radius: the radius of the Gaussian, in pixels, not counting
%      the center pixel.
%
%    o sigma: the standard deviation of the Gaussian, in pixels.
%
%    o angle: Apply the effect along this angle.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *SketchImage(const Image *image,const double radius,
  const double sigma,const double angle,ExceptionInfo *exception)
{
  CacheView
    *random_view;

  Image
    *blend_image,
    *blur_image,
    *dodge_image,
    *random_image,
    *sketch_image;

  MagickBooleanType
    status;

  MagickPixelPacket
    zero;

  RandomInfo
    **magick_restrict random_info;

  ssize_t
    y;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
  unsigned long
    key;
#endif

  /*
    Sketch image.
  */
  random_image=CloneImage(image,image->columns << 1,image->rows << 1,
    MagickTrue,exception);
  if (random_image == (Image *) NULL)
    return((Image *) NULL);
  status=MagickTrue;
  GetMagickPixelPacket(random_image,&zero);
  random_info=AcquireRandomInfoThreadSet();
  random_view=AcquireAuthenticCacheView(random_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  key=GetRandomSecretKey(random_info[0]);
#pragma omp parallel for schedule(static) shared(status) \
magick_number_threads(random_image,random_image,random_image->rows,key == ~0UL)
#endif
  for (y=0; y < (ssize_t) random_image->rows; y++)
  {
    const int
      id = GetOpenMPThreadId();

    MagickPixelPacket
      pixel;

    register IndexPacket
      *magick_restrict indexes;

    register ssize_t
      x;

    register PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    q=QueueCacheViewAuthenticPixels(random_view,0,y,random_image->columns,1,
      exception);
    if (q == (PixelPacket *) NULL)
    {
      status=MagickFalse;
      continue;
    }
    indexes=GetCacheViewAuthenticIndexQueue(random_view);
    pixel=zero;
    for (x=0; x < (ssize_t) random_image->columns; x++)
    {
      pixel.red=(MagickRealType) (QuantumRange*
        GetPseudoRandomValue(random_info[id]));
      pixel.green=pixel.red;
      pixel.blue=pixel.red;
      if (image->colorspace == CMYKColorspace)
        pixel.index=pixel.red;
      SetPixelPacket(random_image,&pixel,q,indexes+x);
      q++;
    }
    if (SyncCacheViewAuthenticPixels(random_view,exception) == MagickFalse)
      status=MagickFalse;
  }
  random_info=DestroyRandomInfoThreadSet(random_info);
  if (status == MagickFalse)
    {
      random_view=DestroyCacheView(random_view);
      random_image=DestroyImage(random_image);
      return(random_image);
    }
  random_view=DestroyCacheView(random_view);

  blur_image=MotionBlurImage(random_image,radius,sigma,angle,exception);
  random_image=DestroyImage(random_image);
  if (blur_image == (Image *) NULL)
    return((Image *) NULL);
  dodge_image=EdgeImage(blur_image,radius,exception);
  blur_image=DestroyImage(blur_image);
  if (dodge_image == (Image *) NULL)
    return((Image *) NULL);
  status=ClampImage(dodge_image);
  if (status != MagickFalse)
    status=NormalizeImage(dodge_image);
  if (status != MagickFalse)
    status=NegateImage(dodge_image,MagickFalse);
  if (status != MagickFalse)
    status=TransformImage(&dodge_image,(char *) NULL,"50%");
  sketch_image=CloneImage(image,0,0,MagickTrue,exception);
  if (sketch_image == (Image *) NULL)
    {
      dodge_image=DestroyImage(dodge_image);
      return((Image *) NULL);
    }
  (void) CompositeImage(sketch_image,ColorDodgeCompositeOp,dodge_image,0,0);
  dodge_image=DestroyImage(dodge_image);
  blend_image=CloneImage(image,0,0,MagickTrue,exception);
  if (blend_image == (Image *) NULL)
    {
      sketch_image=DestroyImage(sketch_image);
      return((Image *) NULL);
    }
  (void) SetImageArtifact(blend_image,"compose:args","20x80");
  (void) CompositeImage(sketch_image,BlendCompositeOp,blend_image,0,0);
  blend_image=DestroyImage(blend_image);
  return(sketch_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     S o l a r i z e I m a g e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SolarizeImage() applies a special effect to the image, similar to the effect
%  achieved in a photo darkroom by selectively exposing areas of photo
%  sensitive paper to light.  Threshold ranges from 0 to QuantumRange and is a
%  measure of the extent of the solarization.
%
%  The format of the SolarizeImage method is:
%
%      MagickBooleanType SolarizeImage(Image *image,const double threshold)
%      MagickBooleanType SolarizeImageChannel(Image *image,
%        const ChannelType channel,const double threshold,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o channel: the channel type.
%
%    o threshold:  Define the extent of the solarization.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport MagickBooleanType SolarizeImage(Image *image,
  const double threshold)
{
  MagickBooleanType
    status;

  status=SolarizeImageChannel(image,DefaultChannels,threshold,
    &image->exception);
  return(status);
}

MagickExport MagickBooleanType SolarizeImageChannel(Image *image,
  const ChannelType channel,const double threshold,ExceptionInfo *exception)
{
#define SolarizeImageTag  "Solarize/Image"

  CacheView
    *image_view;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  ssize_t
    y;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  if (IsGrayColorspace(image->colorspace) != MagickFalse)
    (void) SetImageColorspace(image,sRGBColorspace);
  if (image->storage_class == PseudoClass)
    {
      register ssize_t
        i;

      /*
        Solarize colormap.
      */
      for (i=0; i < (ssize_t) image->colors; i++)
      {
        if ((channel & RedChannel) != 0)
          if ((double) image->colormap[i].red > threshold)
            image->colormap[i].red=QuantumRange-image->colormap[i].red;
        if ((channel & GreenChannel) != 0)
          if ((double) image->colormap[i].green > threshold)
            image->colormap[i].green=QuantumRange-image->colormap[i].green;
        if ((channel & BlueChannel) != 0)
          if ((double) image->colormap[i].blue > threshold)
            image->colormap[i].blue=QuantumRange-image->colormap[i].blue;
      }
    }
  /*
    Solarize image.
  */
  status=MagickTrue;
  progress=0;
  image_view=AcquireAuthenticCacheView(image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,image,image->rows,1)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    register ssize_t
      x;

    register PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    q=GetCacheViewAuthenticPixels(image_view,0,y,image->columns,1,
      exception);
    if (q == (PixelPacket *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      if ((channel & RedChannel) != 0)
        if ((double) GetPixelRed(q) > threshold)
          SetPixelRed(q,QuantumRange-GetPixelRed(q));
      if ((channel & GreenChannel) != 0)
        if ((double) GetPixelGreen(q) > threshold)
          SetPixelGreen(q,QuantumRange-GetPixelGreen(q));
      if ((channel & BlueChannel) != 0)
        if ((double) GetPixelBlue(q) > threshold)
          SetPixelBlue(q,QuantumRange-GetPixelBlue(q));
      q++;
    }
    if (SyncCacheViewAuthenticPixels(image_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,SolarizeImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  image_view=DestroyCacheView(image_view);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   S t e g a n o I m a g e                                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SteganoImage() hides a digital watermark within the image.  Recover
%  the hidden watermark later to prove that the authenticity of an image.
%  Offset defines the start position within the image to hide the watermark.
%
%  The format of the SteganoImage method is:
%
%      Image *SteganoImage(const Image *image,Image *watermark,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o watermark: the watermark image.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *SteganoImage(const Image *image,const Image *watermark,
  ExceptionInfo *exception)
{
#define GetBit(alpha,i) ((((size_t) (alpha) >> (size_t) (i)) & 0x01) != 0)
#define SetBit(alpha,i,set) (alpha)=(Quantum) ((set) != 0 ? (size_t) (alpha) \
  | (one << (size_t) (i)) : (size_t) (alpha) & ~(one << (size_t) (i)))
#define SteganoImageTag  "Stegano/Image"

  CacheView
    *stegano_view,
    *watermark_view;

  Image
    *stegano_image;

  int
    c;

  MagickBooleanType
    status;

  PixelPacket
    pixel;

  register PixelPacket
    *q;

  register ssize_t
    x;

  size_t
    depth,
    one;

  ssize_t
    i,
    j,
    k,
    y;

  /*
    Initialize steganographic image attributes.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(watermark != (const Image *) NULL);
  assert(watermark->signature == MagickCoreSignature);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  one=1UL;
  stegano_image=CloneImage(image,0,0,MagickTrue,exception);
  if (stegano_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(stegano_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&stegano_image->exception);
      stegano_image=DestroyImage(stegano_image);
      return((Image *) NULL);
    }
  stegano_image->depth=MAGICKCORE_QUANTUM_DEPTH;
  /*
    Hide watermark in low-order bits of image.
  */
  c=0;
  i=0;
  j=0;
  depth=stegano_image->depth;
  k=image->offset;
  status=MagickTrue;
  watermark_view=AcquireVirtualCacheView(watermark,exception);
  stegano_view=AcquireAuthenticCacheView(stegano_image,exception);
  for (i=(ssize_t) depth-1; (i >= 0) && (j < (ssize_t) depth); i--)
  {
    for (y=0; (y < (ssize_t) watermark->rows) && (j < (ssize_t) depth); y++)
    {
      for (x=0; (x < (ssize_t) watermark->columns) && (j < (ssize_t) depth); x++)
      {
        (void) GetOneCacheViewVirtualPixel(watermark_view,x,y,&pixel,exception);
        if ((k/(ssize_t) stegano_image->columns) >= (ssize_t) stegano_image->rows)
          break;
        q=GetCacheViewAuthenticPixels(stegano_view,k % (ssize_t)
          stegano_image->columns,k/(ssize_t) stegano_image->columns,1,1,
          exception);
        if (q == (PixelPacket *) NULL)
          break;
        switch (c)
        {
          case 0:
          {
            SetBit(GetPixelRed(q),j,GetBit(ClampToQuantum(GetPixelIntensity(
              image,&pixel)),i));
            break;
          }
          case 1:
          {
            SetBit(GetPixelGreen(q),j,GetBit(ClampToQuantum(GetPixelIntensity(
              image,&pixel)),i));
            break;
          }
          case 2:
          {
            SetBit(GetPixelBlue(q),j,GetBit(ClampToQuantum(GetPixelIntensity(
              image,&pixel)),i));
            break;
          }
        }
        if (SyncCacheViewAuthenticPixels(stegano_view,exception) == MagickFalse)
          break;
        c++;
        if (c == 3)
          c=0;
        k++;
        if (k == (ssize_t) (stegano_image->columns*stegano_image->columns))
          k=0;
        if (k == image->offset)
          j++;
      }
    }
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

        proceed=SetImageProgress(image,SteganoImageTag,(MagickOffsetType)
          (depth-i),depth);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  stegano_view=DestroyCacheView(stegano_view);
  watermark_view=DestroyCacheView(watermark_view);
  if (stegano_image->storage_class == PseudoClass)
    (void) SyncImage(stegano_image);
  if (status == MagickFalse)
    stegano_image=DestroyImage(stegano_image);
  return(stegano_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   S t e r e o A n a g l y p h I m a g e                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  StereoAnaglyphImage() combines two images and produces a single image that
%  is the composite of a left and right image of a stereo pair.  Special
%  red-green stereo glasses are required to view this effect.
%
%  The format of the StereoAnaglyphImage method is:
%
%      Image *StereoImage(const Image *left_image,const Image *right_image,
%        ExceptionInfo *exception)
%      Image *StereoAnaglyphImage(const Image *left_image,
%        const Image *right_image,const ssize_t x_offset,const ssize_t y_offset,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o left_image: the left image.
%
%    o right_image: the right image.
%
%    o exception: return any errors or warnings in this structure.
%
%    o x_offset: amount, in pixels, by which the left image is offset to the
%      right of the right image.
%
%    o y_offset: amount, in pixels, by which the left image is offset to the
%      bottom of the right image.
%
%
*/
MagickExport Image *StereoImage(const Image *left_image,
  const Image *right_image,ExceptionInfo *exception)
{
  return(StereoAnaglyphImage(left_image,right_image,0,0,exception));
}

MagickExport Image *StereoAnaglyphImage(const Image *left_image,
  const Image *right_image,const ssize_t x_offset,const ssize_t y_offset,
  ExceptionInfo *exception)
{
#define StereoImageTag  "Stereo/Image"

  const Image
    *image;

  Image
    *stereo_image;

  MagickBooleanType
    status;

  ssize_t
    y;

  assert(left_image != (const Image *) NULL);
  assert(left_image->signature == MagickCoreSignature);
  if (left_image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      left_image->filename);
  assert(right_image != (const Image *) NULL);
  assert(right_image->signature == MagickCoreSignature);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  image=left_image;
  if ((left_image->columns != right_image->columns) ||
      (left_image->rows != right_image->rows))
    ThrowImageException(ImageError,"LeftAndRightImageSizesDiffer");
  /*
    Initialize stereo image attributes.
  */
  stereo_image=CloneImage(left_image,left_image->columns,left_image->rows,
    MagickTrue,exception);
  if (stereo_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(stereo_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&stereo_image->exception);
      stereo_image=DestroyImage(stereo_image);
      return((Image *) NULL);
    }
  (void) SetImageColorspace(stereo_image,sRGBColorspace);
  /*
    Copy left image to red channel and right image to blue channel.
  */
  status=MagickTrue;
  for (y=0; y < (ssize_t) stereo_image->rows; y++)
  {
    register const PixelPacket
      *magick_restrict p,
      *magick_restrict q;

    register ssize_t
      x;

    register PixelPacket
      *magick_restrict r;

    p=GetVirtualPixels(left_image,-x_offset,y-y_offset,image->columns,1,
      exception);
    q=GetVirtualPixels(right_image,0,y,right_image->columns,1,exception);
    r=QueueAuthenticPixels(stereo_image,0,y,stereo_image->columns,1,exception);
    if ((p == (PixelPacket *) NULL) || (q == (PixelPacket *) NULL) ||
        (r == (PixelPacket *) NULL))
      break;
    for (x=0; x < (ssize_t) stereo_image->columns; x++)
    {
      SetPixelRed(r,GetPixelRed(p));
      SetPixelGreen(r,GetPixelGreen(q));
      SetPixelBlue(r,GetPixelBlue(q));
      SetPixelOpacity(r,(GetPixelOpacity(p)+q->opacity)/2);
      p++;
      q++;
      r++;
    }
    if (SyncAuthenticPixels(stereo_image,exception) == MagickFalse)
      break;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

        proceed=SetImageProgress(image,StereoImageTag,(MagickOffsetType) y,
          stereo_image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  if (status == MagickFalse)
    stereo_image=DestroyImage(stereo_image);
  return(stereo_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     S w i r l I m a g e                                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SwirlImage() swirls the pixels about the center of the image, where
%  degrees indicates the sweep of the arc through which each pixel is moved.
%  You get a more dramatic effect as the degrees move from 1 to 360.
%
%  The format of the SwirlImage method is:
%
%      Image *SwirlImage(const Image *image,double degrees,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o degrees: Define the tightness of the swirling effect.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *SwirlImage(const Image *image,double degrees,
  ExceptionInfo *exception)
{
#define SwirlImageTag  "Swirl/Image"

  CacheView
    *image_view,
    *swirl_view;

  double
    radius;

  Image
    *swirl_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  MagickPixelPacket
    zero;

  PointInfo
    center,
    scale;

  ssize_t
    y;

  /*
    Initialize swirl image attributes.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  swirl_image=CloneImage(image,0,0,MagickTrue,exception);
  if (swirl_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(swirl_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&swirl_image->exception);
      swirl_image=DestroyImage(swirl_image);
      return((Image *) NULL);
    }
  if (swirl_image->background_color.opacity != OpaqueOpacity)
    swirl_image->matte=MagickTrue;
  /*
    Compute scaling factor.
  */
  center.x=(double) image->columns/2.0;
  center.y=(double) image->rows/2.0;
  radius=MagickMax(center.x,center.y);
  scale.x=1.0;
  scale.y=1.0;
  if (image->columns > image->rows)
    scale.y=(double) image->columns/(double) image->rows;
  else
    if (image->columns < image->rows)
      scale.x=(double) image->rows/(double) image->columns;
  degrees=(double) DegreesToRadians(degrees);
  /*
    Swirl image.
  */
  status=MagickTrue;
  progress=0;
  GetMagickPixelPacket(swirl_image,&zero);
  image_view=AcquireVirtualCacheView(image,exception);
  swirl_view=AcquireAuthenticCacheView(swirl_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,swirl_image,image->rows,1)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    double
      distance;

    MagickPixelPacket
      pixel;

    PointInfo
      delta;

    register IndexPacket
      *magick_restrict swirl_indexes;

    register ssize_t
      x;

    register PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    q=GetCacheViewAuthenticPixels(swirl_view,0,y,swirl_image->columns,1,
      exception);
    if (q == (PixelPacket *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    swirl_indexes=GetCacheViewAuthenticIndexQueue(swirl_view);
    delta.y=scale.y*(double) (y-center.y);
    pixel=zero;
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      /*
        Determine if the pixel is within an ellipse.
      */
      delta.x=scale.x*(double) (x-center.x);
      distance=delta.x*delta.x+delta.y*delta.y;
      if (distance < (radius*radius))
        {
          double
            cosine,
            factor,
            sine;

          /*
            Swirl the pixel.
          */
          factor=1.0-sqrt(distance)/radius;
          sine=sin((double) (degrees*factor*factor));
          cosine=cos((double) (degrees*factor*factor));
          status=InterpolateMagickPixelPacket(image,image_view,
            UndefinedInterpolatePixel,(double) ((cosine*delta.x-sine*delta.y)/
            scale.x+center.x),(double) ((sine*delta.x+cosine*delta.y)/scale.y+
            center.y),&pixel,exception);
          if (status == MagickFalse)
            break;
          SetPixelPacket(swirl_image,&pixel,q,swirl_indexes+x);
        }
      q++;
    }
    if (SyncCacheViewAuthenticPixels(swirl_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,SwirlImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  swirl_view=DestroyCacheView(swirl_view);
  image_view=DestroyCacheView(image_view);
  if (status == MagickFalse)
    swirl_image=DestroyImage(swirl_image);
  return(swirl_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     T i n t I m a g e                                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  TintImage() applies a color vector to each pixel in the image.  The length
%  of the vector is 0 for black and white and at its maximum for the midtones.
%  The vector weighting function is f(x)=(1-(4.0*((x-0.5)*(x-0.5))))
%
%  The format of the TintImage method is:
%
%      Image *TintImage(const Image *image,const char *opacity,
%        const PixelPacket tint,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o opacity: A color value used for tinting.
%
%    o tint: A color value used for tinting.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *TintImage(const Image *image,const char *opacity,
  const PixelPacket tint,ExceptionInfo *exception)
{
#define TintImageTag  "Tint/Image"

  CacheView
    *image_view,
    *tint_view;

  GeometryInfo
    geometry_info;

  Image
    *tint_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  MagickPixelPacket
    color_vector,
    pixel;

  MagickStatusType
    flags;

  ssize_t
    y;

  /*
    Allocate tint image.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  tint_image=CloneImage(image,0,0,MagickTrue,exception);
  if (tint_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(tint_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&tint_image->exception);
      tint_image=DestroyImage(tint_image);
      return((Image *) NULL);
    }
  if ((IsGrayColorspace(image->colorspace) != MagickFalse) &&
      (IsPixelGray(&tint) == MagickFalse))
    (void) SetImageColorspace(tint_image,sRGBColorspace);
  if (opacity == (const char *) NULL)
    return(tint_image);
  /*
    Determine RGB values of the tint color.
  */
  flags=ParseGeometry(opacity,&geometry_info);
  pixel.red=geometry_info.rho;
  pixel.green=geometry_info.rho;
  pixel.blue=geometry_info.rho;
  pixel.opacity=(MagickRealType) OpaqueOpacity;
  if ((flags & SigmaValue) != 0)
    pixel.green=geometry_info.sigma;
  if ((flags & XiValue) != 0)
    pixel.blue=geometry_info.xi;
  if ((flags & PsiValue) != 0)
    pixel.opacity=geometry_info.psi;
  color_vector.red=(MagickRealType) (pixel.red*tint.red/100.0-
    PixelPacketIntensity(&tint));
  color_vector.green=(MagickRealType) (pixel.green*tint.green/100.0-
    PixelPacketIntensity(&tint));
  color_vector.blue=(MagickRealType) (pixel.blue*tint.blue/100.0-
    PixelPacketIntensity(&tint));
  /*
    Tint image.
  */
  status=MagickTrue;
  progress=0;
  image_view=AcquireVirtualCacheView(image,exception);
  tint_view=AcquireAuthenticCacheView(tint_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,tint_image,image->rows,1)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    register const PixelPacket
      *magick_restrict p;

    register PixelPacket
      *magick_restrict q;

    register ssize_t
      x;

    if (status == MagickFalse)
      continue;
    p=GetCacheViewVirtualPixels(image_view,0,y,image->columns,1,exception);
    q=QueueCacheViewAuthenticPixels(tint_view,0,y,tint_image->columns,1,
      exception);
    if ((p == (const PixelPacket *) NULL) || (q == (PixelPacket *) NULL))
      {
        status=MagickFalse;
        continue;
      }
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      double
        weight;

      MagickPixelPacket
        pixel;

      weight=QuantumScale*GetPixelRed(p)-0.5;
      pixel.red=(MagickRealType) GetPixelRed(p)+color_vector.red*(1.0-(4.0*
        (weight*weight)));
      SetPixelRed(q,ClampToQuantum(pixel.red));
      weight=QuantumScale*GetPixelGreen(p)-0.5;
      pixel.green=(MagickRealType) GetPixelGreen(p)+color_vector.green*(1.0-
        (4.0*(weight*weight)));
      SetPixelGreen(q,ClampToQuantum(pixel.green));
      weight=QuantumScale*GetPixelBlue(p)-0.5;
      pixel.blue=(MagickRealType) GetPixelBlue(p)+color_vector.blue*(1.0-(4.0*
        (weight*weight)));
      SetPixelBlue(q,ClampToQuantum(pixel.blue));
      SetPixelOpacity(q,GetPixelOpacity(p));
      p++;
      q++;
    }
    if (SyncCacheViewAuthenticPixels(tint_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,TintImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  tint_view=DestroyCacheView(tint_view);
  image_view=DestroyCacheView(image_view);
  if (status == MagickFalse)
    tint_image=DestroyImage(tint_image);
  return(tint_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     V i g n e t t e I m a g e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  VignetteImage() softens the edges of the image in vignette style.
%
%  The format of the VignetteImage method is:
%
%      Image *VignetteImage(const Image *image,const double radius,
%        const double sigma,const ssize_t x,const ssize_t y,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o radius: the radius of the pixel neighborhood.
%
%    o sigma: the standard deviation of the Gaussian, in pixels.
%
%    o x, y:  Define the x and y ellipse offset.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *VignetteImage(const Image *image,const double radius,
  const double sigma,const ssize_t x,const ssize_t y,ExceptionInfo *exception)
{
  char
    ellipse[MaxTextExtent];

  DrawInfo
    *draw_info;

  Image
    *blur_image,
    *canvas_image,
    *oval_image,
    *vignette_image;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  canvas_image=CloneImage(image,0,0,MagickTrue,exception);
  if (canvas_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(canvas_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&canvas_image->exception);
      canvas_image=DestroyImage(canvas_image);
      return((Image *) NULL);
    }
  canvas_image->matte=MagickTrue;
  oval_image=CloneImage(canvas_image,canvas_image->columns,canvas_image->rows,
    MagickTrue,exception);
  if (oval_image == (Image *) NULL)
    {
      canvas_image=DestroyImage(canvas_image);
      return((Image *) NULL);
    }
  (void) QueryColorDatabase("#000000",&oval_image->background_color,exception);
  (void) SetImageBackgroundColor(oval_image);
  draw_info=CloneDrawInfo((const ImageInfo *) NULL,(const DrawInfo *) NULL);
  (void) QueryColorDatabase("#ffffff",&draw_info->fill,exception);
  (void) QueryColorDatabase("#ffffff",&draw_info->stroke,exception);
  (void) FormatLocaleString(ellipse,MaxTextExtent,
    "ellipse %g,%g,%g,%g,0.0,360.0",image->columns/2.0,
    image->rows/2.0,image->columns/2.0-x,image->rows/2.0-y);
  draw_info->primitive=AcquireString(ellipse);
  (void) DrawImage(oval_image,draw_info);
  draw_info=DestroyDrawInfo(draw_info);
  blur_image=BlurImage(oval_image,radius,sigma,exception);
  oval_image=DestroyImage(oval_image);
  if (blur_image == (Image *) NULL)
    {
      canvas_image=DestroyImage(canvas_image);
      return((Image *) NULL);
    }
  blur_image->matte=MagickFalse;
  (void) CompositeImage(canvas_image,CopyOpacityCompositeOp,blur_image,0,0);
  blur_image=DestroyImage(blur_image);
  vignette_image=MergeImageLayers(canvas_image,FlattenLayer,exception);
  canvas_image=DestroyImage(canvas_image);
  if (vignette_image != (Image *) NULL)
    (void) TransformImageColorspace(vignette_image,image->colorspace);
  return(vignette_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     W a v e I m a g e                                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  WaveImage() creates a "ripple" effect in the image by shifting the pixels
%  vertically along a sine wave whose amplitude and wavelength is specified
%  by the given parameters.
%
%  The format of the WaveImage method is:
%
%      Image *WaveImage(const Image *image,const double amplitude,
%        const double wave_length,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o amplitude, wave_length:  Define the amplitude and wave length of the
%      sine wave.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport Image *WaveImage(const Image *image,const double amplitude,
  const double wave_length,ExceptionInfo *exception)
{
#define WaveImageTag  "Wave/Image"

  CacheView
    *image_view,
    *wave_view;

  float
    *sine_map;

  Image
    *wave_image;

  MagickBooleanType
    status;

  MagickOffsetType
    progress;

  MagickPixelPacket
    zero;

  register ssize_t
    i;

  ssize_t
    y;

  /*
    Initialize wave image attributes.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  wave_image=CloneImage(image,image->columns,(size_t) (image->rows+2.0*
    fabs(amplitude)),MagickTrue,exception);
  if (wave_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(wave_image,DirectClass) == MagickFalse)
    {
      InheritException(exception,&wave_image->exception);
      wave_image=DestroyImage(wave_image);
      return((Image *) NULL);
    }
  if (wave_image->background_color.opacity != OpaqueOpacity)
    wave_image->matte=MagickTrue;
  /*
    Allocate sine map.
  */
  sine_map=(float *) AcquireQuantumMemory((size_t) wave_image->columns,
    sizeof(*sine_map));
  if (sine_map == (float *) NULL)
    {
      wave_image=DestroyImage(wave_image);
      ThrowImageException(ResourceLimitError,"MemoryAllocationFailed");
    }
  for (i=0; i < (ssize_t) wave_image->columns; i++)
    sine_map[i]=(float) fabs(amplitude)+amplitude*sin((double)
      ((2.0*MagickPI*i)/wave_length));
  /*
    Wave image.
  */
  status=MagickTrue;
  progress=0;
  GetMagickPixelPacket(wave_image,&zero);
  image_view=AcquireVirtualCacheView(image,exception);
  wave_view=AcquireAuthenticCacheView(wave_image,exception);
  (void) SetCacheViewVirtualPixelMethod(image_view,
    BackgroundVirtualPixelMethod);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(image,wave_image,wave_image->rows,1)
#endif
  for (y=0; y < (ssize_t) wave_image->rows; y++)
  {
    MagickPixelPacket
      pixel;

    register IndexPacket
      *magick_restrict indexes;

    register PixelPacket
      *magick_restrict q;

    register ssize_t
      x;

    if (status == MagickFalse)
      continue;
    q=QueueCacheViewAuthenticPixels(wave_view,0,y,wave_image->columns,1,
      exception);
    if (q == (PixelPacket *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    indexes=GetCacheViewAuthenticIndexQueue(wave_view);
    pixel=zero;
    for (x=0; x < (ssize_t) wave_image->columns; x++)
    {
      status=InterpolateMagickPixelPacket(image,image_view,
        UndefinedInterpolatePixel,(double) x,(double) (y-sine_map[x]),&pixel,
        exception);
      if (status == MagickFalse)
        break;
      SetPixelPacket(wave_image,&pixel,q,indexes+x);
      q++;
    }
    if (SyncCacheViewAuthenticPixels(wave_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,WaveImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  wave_view=DestroyCacheView(wave_view);
  image_view=DestroyCacheView(image_view);
  sine_map=(float *) RelinquishMagickMemory(sine_map);
  if (status == MagickFalse)
    wave_image=DestroyImage(wave_image);
  return(wave_image);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     W a v e l e t D e n o i s e I m a g e                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  WaveletDenoiseImage() removes noise from the image using a wavelet
%  transform.  The wavelet transform is a fast hierarchical scheme for
%  processing an image using a set of consecutive lowpass and high_pass filters,
%  followed by a decimation.  This results in a decomposition into different
%  scales which can be regarded as different “frequency bands”, determined by
%  the mother wavelet.  Adapted from dcraw.c by David Coffin.
%
%  The format of the WaveletDenoiseImage method is:
%
%      Image *WaveletDenoiseImage(const Image *image,const double threshold,
%        const double softness,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o threshold: set the threshold for smoothing.
%
%    o softness: attenuate the smoothing threshold.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static inline void HatTransform(const float *magick_restrict pixels,
  const size_t stride,const size_t extent,const size_t scale,float *kernel)
{
  const float
    *magick_restrict p,
    *magick_restrict q,
    *magick_restrict r;

  register ssize_t
    i;

  p=pixels;
  q=pixels+scale*stride,
  r=pixels+scale*stride;
  for (i=0; i < (ssize_t) scale; i++)
  {
    kernel[i]=0.25f*(*p+(*p)+(*q)+(*r));
    p+=stride;
    q-=stride;
    r+=stride;
  }
  for ( ; i < (ssize_t) (extent-scale); i++)
  {
    kernel[i]=0.25f*(2.0f*(*p)+*(p-scale*stride)+*(p+scale*stride));
    p+=stride;
  }
  q=p-scale*stride;
  r=pixels+stride*(extent-2);
  for ( ; i < (ssize_t) extent; i++)
  {
    kernel[i]=0.25f*(*p+(*p)+(*q)+(*r));
    p+=stride;
    q+=stride;
    r-=stride;
  }
}

MagickExport Image *WaveletDenoiseImage(const Image *image,
  const double threshold,const double softness,ExceptionInfo *exception)
{
  CacheView
    *image_view,
    *noise_view;

  float
    *kernel,
    *pixels;

  Image
    *noise_image;

  MagickBooleanType
    status;

  MagickSizeType
    number_pixels;

  MemoryInfo
    *pixels_info;

  size_t
    max_channels;

  ssize_t
    channel;

  static const double
    noise_levels[]= {
      0.8002, 0.2735, 0.1202, 0.0585, 0.0291, 0.0152, 0.0080, 0.0044 };

  /*
    Initialize noise image attributes.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  noise_image=(Image *) NULL;
#if defined(MAGICKCORE_OPENCL_SUPPORT)
  noise_image=AccelerateWaveletDenoiseImage(image,threshold,exception);
  if (noise_image != (Image *) NULL)
    return(noise_image);
#endif
  noise_image=CloneImage(image,0,0,MagickTrue,exception);
  if (noise_image == (Image *) NULL)
    return((Image *) NULL);
  if (SetImageStorageClass(noise_image,DirectClass) == MagickFalse)
    {
      noise_image=DestroyImage(noise_image);
      return((Image *) NULL);
    }
  if (AcquireMagickResource(WidthResource,3*image->columns) == MagickFalse)
    ThrowImageException(ResourceLimitError,"MemoryAllocationFailed");
  pixels_info=AcquireVirtualMemory(3*image->columns,image->rows*
    sizeof(*pixels));
  kernel=(float *) AcquireQuantumMemory(MagickMax(image->rows,image->columns)+1,
    GetOpenMPMaximumThreads()*sizeof(*kernel));
  if ((pixels_info == (MemoryInfo *) NULL) || (kernel == (float *) NULL))
    {
      if (kernel != (float *) NULL)
        kernel=(float *) RelinquishMagickMemory(kernel);
      if (pixels_info != (MemoryInfo *) NULL)
        pixels_info=RelinquishVirtualMemory(pixels_info);
      ThrowImageException(ResourceLimitError,"MemoryAllocationFailed");
    }
  pixels=(float *) GetVirtualMemoryBlob(pixels_info);
  status=MagickTrue;
  number_pixels=image->columns*image->rows;
  max_channels=(size_t) (image->colorspace == CMYKColorspace ? 4 : 3);
  image_view=AcquireAuthenticCacheView(image,exception);
  noise_view=AcquireAuthenticCacheView(noise_image,exception);
  for (channel=0; channel < (ssize_t) max_channels; channel++)
  {
    register ssize_t
      i;

    size_t
      high_pass,
      low_pass;

    ssize_t
      level,
      y;

    if (status == MagickFalse)
      continue;
    /*
      Copy channel from image to wavelet pixel array.
    */
    i=0;
    for (y=0; y < (ssize_t) image->rows; y++)
    {
      register const IndexPacket
        *magick_restrict indexes;

      register const PixelPacket
        *magick_restrict p;

      ssize_t
        x;

      p=GetCacheViewAuthenticPixels(image_view,0,y,image->columns,1,exception);
      if (p == (const PixelPacket *) NULL)
        {
          status=MagickFalse;
          break;
        }
      indexes=GetCacheViewVirtualIndexQueue(image_view);
      for (x=0; x < (ssize_t) image->columns; x++)
      {
        switch (channel)
        {
          case 0: pixels[i]=(float) GetPixelRed(p); break;
          case 1: pixels[i]=(float) GetPixelGreen(p); break;
          case 2: pixels[i]=(float) GetPixelBlue(p); break;
          case 3: pixels[i]=(float) indexes[x]; break;
          default: break;
        }
        i++;
        p++;
      }
    }
    /*
      Low pass filter outputs are called approximation kernel & high pass
      filters are referred to as detail kernel. The detail kernel
      have high values in the noisy parts of the signal.
    */
    high_pass=0;
    for (level=0; level < 5; level++)
    {
      double
        magnitude;

      ssize_t
        x,
        y;

      low_pass=(size_t) (number_pixels*((level & 0x01)+1));
#if defined(MAGICKCORE_OPENMP_SUPPORT)
      #pragma omp parallel for schedule(static,1) \
        magick_number_threads(image,image,image->rows,1)
#endif
      for (y=0; y < (ssize_t) image->rows; y++)
      {
        const int
          id = GetOpenMPThreadId();

        register float
          *magick_restrict p,
          *magick_restrict q;

        register ssize_t
          x;

        p=kernel+id*image->columns;
        q=pixels+y*image->columns;
        HatTransform(q+high_pass,1,image->columns,(size_t) (1UL << level),p);
        q+=low_pass;
        for (x=0; x < (ssize_t) image->columns; x++)
          *q++=(*p++);
      }
#if defined(MAGICKCORE_OPENMP_SUPPORT)
      #pragma omp parallel for schedule(static,1) \
        magick_number_threads(image,image,image->columns,1)
#endif
      for (x=0; x < (ssize_t) image->columns; x++)
      {
        const int
          id = GetOpenMPThreadId();

        register float
          *magick_restrict p,
          *magick_restrict q;

        register ssize_t
          y;

        p=kernel+id*image->rows;
        q=pixels+x+low_pass;
        HatTransform(q,image->columns,image->rows,(size_t) (1UL << level),p);
        for (y=0; y < (ssize_t) image->rows; y++)
        {
          *q=(*p++);
          q+=image->columns;
        }
      }
      /*
        To threshold, each coefficient is compared to a threshold value and
        attenuated / shrunk by some factor.
      */
      magnitude=threshold*noise_levels[level];
      for (i=0; i < (ssize_t) number_pixels; ++i)
      {
        pixels[high_pass+i]-=pixels[low_pass+i];
        if (pixels[high_pass+i] < -magnitude)
          pixels[high_pass+i]+=magnitude-softness*magnitude;
        else
          if (pixels[high_pass+i] > magnitude)
            pixels[high_pass+i]-=magnitude-softness*magnitude;
          else
            pixels[high_pass+i]*=softness;
        if (high_pass != 0)
          pixels[i]+=pixels[high_pass+i];
      }
      high_pass=low_pass;
    }
    /*
      Reconstruct image from the thresholded wavelet kernel.
    */
    i=0;
    for (y=0; y < (ssize_t) image->rows; y++)
    {
      MagickBooleanType
        sync;

      register IndexPacket
        *magick_restrict noise_indexes;

      register PixelPacket
        *magick_restrict q;

      register ssize_t
        x;

      q=GetCacheViewAuthenticPixels(noise_view,0,y,noise_image->columns,1,
        exception);
      if (q == (PixelPacket *) NULL)
        {
          status=MagickFalse;
          break;
        }
      noise_indexes=GetCacheViewAuthenticIndexQueue(noise_view);
      for (x=0; x < (ssize_t) image->columns; x++)
      {
        float
          pixel;

        pixel=pixels[i]+pixels[low_pass+i];
        switch (channel)
        {
          case 0: SetPixelRed(q,ClampToQuantum(pixel)); break;
          case 1: SetPixelGreen(q,ClampToQuantum(pixel)); break;
          case 2: SetPixelBlue(q,ClampToQuantum(pixel)); break;
          case 3: SetPixelIndex(noise_indexes+x,ClampToQuantum(pixel)); break;
          default: break;
        }
        i++;
        q++;
      }
      sync=SyncCacheViewAuthenticPixels(noise_view,exception);
      if (sync == MagickFalse)
        status=MagickFalse;
    }
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

        proceed=SetImageProgress(image,AddNoiseImageTag,(MagickOffsetType)
          channel,max_channels);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  noise_view=DestroyCacheView(noise_view);
  image_view=DestroyCacheView(image_view);
  kernel=(float *) RelinquishMagickMemory(kernel);
  pixels_info=RelinquishVirtualMemory(pixels_info);
  return(noise_image);
}
