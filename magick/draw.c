/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%                        DDDD   RRRR    AAA   W   W                           %
%                        D   D  R   R  A   A  W   W                           %
%                        D   D  RRRR   AAAAA  W W W                           %
%                        D   D  R RN   A   A  WW WW                           %
%                        DDDD   R  R   A   A  W   W                           %
%                                                                             %
%                                                                             %
%                     MagickCore Image Drawing Methods                        %
%                                                                             %
%                                                                             %
%                              Software Design                                %
%                                   Cristy                                    %
%                                 July 1998                                   %
%                                                                             %
%                                                                             %
%  Copyright 1999-2018 ImageMagick Studio LLC, a non-profit organization      %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    https://www.imagemagick.org/script/license.php                           %
%                                                                             %
%  Unless required by applicable law or agreed to in writing, software        %
%  distributed under the License is distributed on an "AS IS" BASIS,          %
%  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
%  See the License for the specific language governing permissions and        %
%  limitations under the License.                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
% Bill Radcliffe of Corbis (www.corbis.com) contributed the polygon
% rendering code based on Paul Heckbert's "Concave Polygon Scan Conversion",
% Graphics Gems, 1990.  Leonard Rosenthal and David Harr of Appligent
% (www.appligent.com) contributed the dash pattern, linecap stroking
% algorithm, and minor rendering improvements.
%
*/

/*
  Include declarations.
*/
#include "magick/studio.h"
#include "magick/annotate.h"
#include "magick/artifact.h"
#include "magick/blob.h"
#include "magick/cache.h"
#include "magick/cache-private.h"
#include "magick/cache-view.h"
#include "magick/channel.h"
#include "magick/color.h"
#include "magick/color-private.h"
#include "magick/colorspace.h"
#include "magick/colorspace-private.h"
#include "magick/composite.h"
#include "magick/composite-private.h"
#include "magick/constitute.h"
#include "magick/draw.h"
#include "magick/draw-private.h"
#include "magick/enhance.h"
#include "magick/exception.h"
#include "magick/exception-private.h"
#include "magick/gem.h"
#include "magick/geometry.h"
#include "magick/image-private.h"
#include "magick/list.h"
#include "magick/log.h"
#include "magick/memory-private.h"
#include "magick/monitor.h"
#include "magick/monitor-private.h"
#include "magick/option.h"
#include "magick/paint.h"
#include "magick/pixel-accessor.h"
#include "magick/pixel-private.h"
#include "magick/property.h"
#include "magick/resample.h"
#include "magick/resample-private.h"
#include "magick/resource_.h"
#include "magick/string_.h"
#include "magick/string-private.h"
#include "magick/thread-private.h"
#include "magick/token.h"
#include "magick/transform.h"
#include "magick/utility.h"

/*
  Define declarations.
*/
#define BezierQuantum  200
#define DrawEpsilon  (1.0e-10)
#define MaxBezierCoordinates  2097152
#define ThrowPointExpectedException(image,token) \
{ \
  (void) ThrowMagickException(&(image)->exception,GetMagickModule(),DrawError, \
    "NonconformingDrawingPrimitiveDefinition","`%s'",token); \
  status=MagickFalse; \
  break; \
}

/*
  Typedef declarations.
*/
typedef struct _EdgeInfo
{
  SegmentInfo
    bounds;

  double
    scanline;

  PointInfo
    *points;

  size_t
    number_points;

  ssize_t
    direction;

  MagickBooleanType
    ghostline;

  size_t
    highwater;
} EdgeInfo;

typedef struct _ElementInfo
{
  double
    cx,
    cy,
    major,
    minor,
    angle;
} ElementInfo;

typedef struct _PolygonInfo
{
  EdgeInfo
    *edges;

  size_t
    number_edges;
} PolygonInfo;

typedef enum
{
  MoveToCode,
  OpenCode,
  GhostlineCode,
  LineToCode,
  EndCode
} PathInfoCode;

typedef struct _PathInfo
{
  PointInfo
    point;

  PathInfoCode
    code;
} PathInfo;

/*
  Forward declarations.
*/
static Image
  *DrawClippingMask(Image *,const DrawInfo *,const char *,const char *,
    ExceptionInfo *);

static MagickBooleanType
  DrawStrokePolygon(Image *,const DrawInfo *,const PrimitiveInfo *);

static PrimitiveInfo
  *TraceStrokePolygon(const Image *,const DrawInfo *,const PrimitiveInfo *);

static size_t
  TracePath(Image *,PrimitiveInfo *,const char *);

static void
  TraceArc(PrimitiveInfo *,const PointInfo,const PointInfo,const PointInfo),
  TraceArcPath(PrimitiveInfo *,const PointInfo,const PointInfo,const PointInfo,
    const double,const MagickBooleanType,const MagickBooleanType),
  TraceBezier(PrimitiveInfo *,const size_t),
  TraceCircle(PrimitiveInfo *,const PointInfo,const PointInfo),
  TraceEllipse(PrimitiveInfo *,const PointInfo,const PointInfo,const PointInfo),
  TraceLine(PrimitiveInfo *,const PointInfo,const PointInfo),
  TraceRectangle(PrimitiveInfo *,const PointInfo,const PointInfo),
  TraceRoundRectangle(PrimitiveInfo *,const PointInfo,const PointInfo,
    PointInfo),
  TraceSquareLinecap(PrimitiveInfo *,const size_t,const double);

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   A c q u i r e D r a w I n f o                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AcquireDrawInfo() returns a DrawInfo structure properly initialized.
%
%  The format of the AcquireDrawInfo method is:
%
%      DrawInfo *AcquireDrawInfo(void)
%
*/
MagickExport DrawInfo *AcquireDrawInfo(void)
{
  DrawInfo
    *draw_info;

  draw_info=(DrawInfo *) AcquireCriticalMemory(sizeof(*draw_info));
  GetDrawInfo((ImageInfo *) NULL,draw_info);
  return(draw_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   C l o n e D r a w I n f o                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  CloneDrawInfo() makes a copy of the given draw_info structure.  If NULL
%  is specified, a new DrawInfo structure is created initialized to default
%  values.
%
%  The format of the CloneDrawInfo method is:
%
%      DrawInfo *CloneDrawInfo(const ImageInfo *image_info,
%        const DrawInfo *draw_info)
%
%  A description of each parameter follows:
%
%    o image_info: the image info.
%
%    o draw_info: the draw info.
%
*/
MagickExport DrawInfo *CloneDrawInfo(const ImageInfo *image_info,
  const DrawInfo *draw_info)
{
  DrawInfo
    *clone_info;

  clone_info=(DrawInfo *) AcquireCriticalMemory(sizeof(*clone_info));
  GetDrawInfo(image_info,clone_info);
  if (draw_info == (DrawInfo *) NULL)
    return(clone_info);
  if (clone_info->primitive != (char *) NULL)
    (void) CloneString(&clone_info->primitive,draw_info->primitive);
  if (draw_info->geometry != (char *) NULL)
    (void) CloneString(&clone_info->geometry,draw_info->geometry);
  clone_info->compliance=draw_info->compliance;
  clone_info->viewbox=draw_info->viewbox;
  clone_info->affine=draw_info->affine;
  clone_info->gravity=draw_info->gravity;
  clone_info->fill=draw_info->fill;
  clone_info->stroke=draw_info->stroke;
  clone_info->stroke_width=draw_info->stroke_width;
  if (draw_info->fill_pattern != (Image *) NULL)
    clone_info->fill_pattern=CloneImage(draw_info->fill_pattern,0,0,MagickTrue,
      &draw_info->fill_pattern->exception);
  else
    if (draw_info->tile != (Image *) NULL)
      clone_info->fill_pattern=CloneImage(draw_info->tile,0,0,MagickTrue,
        &draw_info->tile->exception);
  clone_info->tile=NewImageList();  /* tile is deprecated */
  if (draw_info->stroke_pattern != (Image *) NULL)
    clone_info->stroke_pattern=CloneImage(draw_info->stroke_pattern,0,0,
      MagickTrue,&draw_info->stroke_pattern->exception);
  clone_info->stroke_antialias=draw_info->stroke_antialias;
  clone_info->text_antialias=draw_info->text_antialias;
  clone_info->fill_rule=draw_info->fill_rule;
  clone_info->linecap=draw_info->linecap;
  clone_info->linejoin=draw_info->linejoin;
  clone_info->miterlimit=draw_info->miterlimit;
  clone_info->dash_offset=draw_info->dash_offset;
  clone_info->decorate=draw_info->decorate;
  clone_info->compose=draw_info->compose;
  if (draw_info->text != (char *) NULL)
    (void) CloneString(&clone_info->text,draw_info->text);
  if (draw_info->font != (char *) NULL)
    (void) CloneString(&clone_info->font,draw_info->font);
  if (draw_info->metrics != (char *) NULL)
    (void) CloneString(&clone_info->metrics,draw_info->metrics);
  if (draw_info->family != (char *) NULL)
    (void) CloneString(&clone_info->family,draw_info->family);
  clone_info->style=draw_info->style;
  clone_info->stretch=draw_info->stretch;
  clone_info->weight=draw_info->weight;
  if (draw_info->encoding != (char *) NULL)
    (void) CloneString(&clone_info->encoding,draw_info->encoding);
  clone_info->pointsize=draw_info->pointsize;
  clone_info->kerning=draw_info->kerning;
  clone_info->interline_spacing=draw_info->interline_spacing;
  clone_info->interword_spacing=draw_info->interword_spacing;
  clone_info->direction=draw_info->direction;
  if (draw_info->density != (char *) NULL)
    (void) CloneString(&clone_info->density,draw_info->density);
  clone_info->align=draw_info->align;
  clone_info->undercolor=draw_info->undercolor;
  clone_info->border_color=draw_info->border_color;
  if (draw_info->server_name != (char *) NULL)
    (void) CloneString(&clone_info->server_name,draw_info->server_name);
  if (draw_info->dash_pattern != (double *) NULL)
    {
      register ssize_t
        x;

      for (x=0; fabs(draw_info->dash_pattern[x]) >= DrawEpsilon; x++) ;
      clone_info->dash_pattern=(double *) AcquireQuantumMemory((size_t) x+1UL,
        sizeof(*clone_info->dash_pattern));
      if (clone_info->dash_pattern == (double *) NULL)
        ThrowFatalException(ResourceLimitFatalError,
          "UnableToAllocateDashPattern");
      (void) memcpy(clone_info->dash_pattern,draw_info->dash_pattern,(size_t)
        (x+1)*sizeof(*clone_info->dash_pattern));
    }
  clone_info->gradient=draw_info->gradient;
  if (draw_info->gradient.stops != (StopInfo *) NULL)
    {
      size_t
        number_stops;

      number_stops=clone_info->gradient.number_stops;
      clone_info->gradient.stops=(StopInfo *) AcquireQuantumMemory((size_t)
        number_stops,sizeof(*clone_info->gradient.stops));
      if (clone_info->gradient.stops == (StopInfo *) NULL)
        ThrowFatalException(ResourceLimitFatalError,
          "UnableToAllocateDashPattern");
      (void) memcpy(clone_info->gradient.stops,draw_info->gradient.stops,
        (size_t) number_stops*sizeof(*clone_info->gradient.stops));
    }
  clone_info->bounds=draw_info->bounds;
  clone_info->fill_opacity=draw_info->fill_opacity;
  clone_info->stroke_opacity=draw_info->stroke_opacity;
  clone_info->element_reference=draw_info->element_reference;
  clone_info->clip_path=draw_info->clip_path;
  clone_info->clip_units=draw_info->clip_units;
  if (draw_info->clip_mask != (char *) NULL)
    (void) CloneString(&clone_info->clip_mask,draw_info->clip_mask);
  if (draw_info->clipping_mask != (Image *) NULL)
    clone_info->clipping_mask=CloneImage(draw_info->clipping_mask,0,0,
      MagickTrue,&draw_info->clipping_mask->exception);
  if (draw_info->composite_mask != (Image *) NULL)
    clone_info->composite_mask=CloneImage(draw_info->composite_mask,0,0,
      MagickTrue,&draw_info->composite_mask->exception);
  clone_info->render=draw_info->render;
  clone_info->debug=IsEventLogging();
  return(clone_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C o n v e r t P a t h T o P o l y g o n                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ConvertPathToPolygon() converts a path to the more efficient sorted
%  rendering form.
%
%  The format of the ConvertPathToPolygon method is:
%
%      PolygonInfo *ConvertPathToPolygon(const PathInfo *path_info)
%
%  A description of each parameter follows:
%
%    o Method ConvertPathToPolygon returns the path in a more efficient sorted
%      rendering form of type PolygonInfo.
%
%    o draw_info: Specifies a pointer to an DrawInfo structure.
%
%    o path_info: Specifies a pointer to an PathInfo structure.
%
%
*/

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static int CompareEdges(const void *x,const void *y)
{
  register const EdgeInfo
    *p,
    *q;

  /*
    Compare two edges.
  */
  p=(const EdgeInfo *) x;
  q=(const EdgeInfo *) y;
  if ((p->points[0].y-DrawEpsilon) > q->points[0].y)
    return(1);
  if ((p->points[0].y+DrawEpsilon) < q->points[0].y)
    return(-1);
  if ((p->points[0].x-DrawEpsilon) > q->points[0].x)
    return(1);
  if ((p->points[0].x+DrawEpsilon) < q->points[0].x)
    return(-1);
  if (((p->points[1].x-p->points[0].x)*(q->points[1].y-q->points[0].y)-
       (p->points[1].y-p->points[0].y)*(q->points[1].x-q->points[0].x)) > 0.0)
    return(1);
  return(-1);
}

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

static void LogPolygonInfo(const PolygonInfo *polygon_info)
{
  register EdgeInfo
    *p;

  register ssize_t
    i,
    j;

  (void) LogMagickEvent(DrawEvent,GetMagickModule(),"    begin active-edge");
  p=polygon_info->edges;
  for (i=0; i < (ssize_t) polygon_info->number_edges; i++)
  {
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"      edge %.20g:",
      (double) i);
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"      direction: %s",
      p->direction != MagickFalse ? "down" : "up");
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"      ghostline: %s",
      p->ghostline != MagickFalse ? "transparent" : "opaque");
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),
      "      bounds: %g,%g - %g,%g",p->bounds.x1,p->bounds.y1,
      p->bounds.x2,p->bounds.y2);
    for (j=0; j < (ssize_t) p->number_points; j++)
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),"        %g,%g",
        p->points[j].x,p->points[j].y);
    p++;
  }
  (void) LogMagickEvent(DrawEvent,GetMagickModule(),"    end active-edge");
}

static void ReversePoints(PointInfo *points,const size_t number_points)
{
  PointInfo
    point;

  register ssize_t
    i;

  for (i=0; i < (ssize_t) (number_points >> 1); i++)
  {
    point=points[i];
    points[i]=points[number_points-(i+1)];
    points[number_points-(i+1)]=point;
  }
}

static PolygonInfo *ConvertPathToPolygon(const PathInfo *path_info)
{
  long
    direction,
    next_direction;

  PointInfo
    point,
    *points;

  PolygonInfo
    *polygon_info;

  SegmentInfo
    bounds;

  register ssize_t
    i,
    n;

  MagickBooleanType
    ghostline;

  size_t
    edge,
    number_edges,
    number_points;

  /*
    Convert a path to the more efficient sorted rendering form.
  */
  polygon_info=(PolygonInfo *) AcquireMagickMemory(sizeof(*polygon_info));
  if (polygon_info == (PolygonInfo *) NULL)
    return((PolygonInfo *) NULL);
  number_edges=16;
  polygon_info->edges=(EdgeInfo *) AcquireQuantumMemory(number_edges,
    sizeof(*polygon_info->edges));
  if (polygon_info->edges == (EdgeInfo *) NULL)
    return((PolygonInfo *) NULL);
  (void) memset(polygon_info->edges,0,number_edges*
    sizeof(*polygon_info->edges));
  direction=0;
  edge=0;
  ghostline=MagickFalse;
  n=0;
  number_points=0;
  points=(PointInfo *) NULL;
  (void) memset(&point,0,sizeof(point));
  (void) memset(&bounds,0,sizeof(bounds));
  polygon_info->edges[edge].number_points=(size_t) n;
  polygon_info->edges[edge].scanline=0.0;
  polygon_info->edges[edge].highwater=0;
  polygon_info->edges[edge].ghostline=ghostline;
  polygon_info->edges[edge].direction=(ssize_t) direction;
  polygon_info->edges[edge].points=points;
  polygon_info->edges[edge].bounds=bounds;
  polygon_info->number_edges=0;
  for (i=0; path_info[i].code != EndCode; i++)
  {
    if ((path_info[i].code == MoveToCode) || (path_info[i].code == OpenCode) ||
        (path_info[i].code == GhostlineCode))
      {
        /*
          Move to.
        */
        if ((points != (PointInfo *) NULL) && (n >= 2))
          {
            if (edge == number_edges)
              {
                number_edges<<=1;
                polygon_info->edges=(EdgeInfo *) ResizeQuantumMemory(
                  polygon_info->edges,(size_t) number_edges,
                  sizeof(*polygon_info->edges));
                if (polygon_info->edges == (EdgeInfo *) NULL)
                  return((PolygonInfo *) NULL);
              }
            polygon_info->edges[edge].number_points=(size_t) n;
            polygon_info->edges[edge].scanline=(-1.0);
            polygon_info->edges[edge].highwater=0;
            polygon_info->edges[edge].ghostline=ghostline;
            polygon_info->edges[edge].direction=(ssize_t) (direction > 0);
            if (direction < 0)
              ReversePoints(points,(size_t) n);
            polygon_info->edges[edge].points=points;
            polygon_info->edges[edge].bounds=bounds;
            polygon_info->edges[edge].bounds.y1=points[0].y;
            polygon_info->edges[edge].bounds.y2=points[n-1].y;
            points=(PointInfo *) NULL;
            ghostline=MagickFalse;
            edge++;
          }
        if (points == (PointInfo *) NULL)
          {
            number_points=16;
            points=(PointInfo *) AcquireQuantumMemory((size_t) number_points,
              sizeof(*points));
            if (points == (PointInfo *) NULL)
              return((PolygonInfo *) NULL);
          }
        ghostline=path_info[i].code == GhostlineCode ? MagickTrue : MagickFalse;
        point=path_info[i].point;
        points[0]=point;
        bounds.x1=point.x;
        bounds.x2=point.x;
        direction=0;
        n=1;
        continue;
      }
    /*
      Line to.
    */
    next_direction=((path_info[i].point.y > point.y) ||
      ((fabs(path_info[i].point.y-point.y) < DrawEpsilon) &&
       (path_info[i].point.x > point.x))) ? 1 : -1;
    if ((points != (PointInfo *) NULL) && (direction != 0) &&
        (direction != next_direction))
      {
        /*
          New edge.
        */
        point=points[n-1];
        if (edge == number_edges)
          {
            number_edges<<=1;
            polygon_info->edges=(EdgeInfo *) ResizeQuantumMemory(
              polygon_info->edges,(size_t) number_edges,
              sizeof(*polygon_info->edges));
            if (polygon_info->edges == (EdgeInfo *) NULL)
              return((PolygonInfo *) NULL);
          }
        polygon_info->edges[edge].number_points=(size_t) n;
        polygon_info->edges[edge].scanline=(-1.0);
        polygon_info->edges[edge].highwater=0;
        polygon_info->edges[edge].ghostline=ghostline;
        polygon_info->edges[edge].direction=(ssize_t) (direction > 0);
        if (direction < 0)
          ReversePoints(points,(size_t) n);
        polygon_info->edges[edge].points=points;
        polygon_info->edges[edge].bounds=bounds;
        polygon_info->edges[edge].bounds.y1=points[0].y;
        polygon_info->edges[edge].bounds.y2=points[n-1].y;
        number_points=16;
        points=(PointInfo *) AcquireQuantumMemory((size_t) number_points,
          sizeof(*points));
        if (points == (PointInfo *) NULL)
          return((PolygonInfo *) NULL);
        n=1;
        ghostline=MagickFalse;
        points[0]=point;
        bounds.x1=point.x;
        bounds.x2=point.x;
        edge++;
      }
    direction=next_direction;
    if (points == (PointInfo *) NULL)
      continue;
    if (n == (ssize_t) number_points)
      {
        number_points<<=1;
        points=(PointInfo *) ResizeQuantumMemory(points,(size_t) number_points,
          sizeof(*points));
        if (points == (PointInfo *) NULL)
          return((PolygonInfo *) NULL);
      }
    point=path_info[i].point;
    points[n]=point;
    if (point.x < bounds.x1)
      bounds.x1=point.x;
    if (point.x > bounds.x2)
      bounds.x2=point.x;
    n++;
  }
  if (points != (PointInfo *) NULL)
    {
      if (n < 2)
        points=(PointInfo *) RelinquishMagickMemory(points);
      else
        {
          if (edge == number_edges)
            {
              number_edges<<=1;
              polygon_info->edges=(EdgeInfo *) ResizeQuantumMemory(
                polygon_info->edges,(size_t) number_edges,
                sizeof(*polygon_info->edges));
              if (polygon_info->edges == (EdgeInfo *) NULL)
                return((PolygonInfo *) NULL);
            }
          polygon_info->edges[edge].number_points=(size_t) n;
          polygon_info->edges[edge].scanline=(-1.0);
          polygon_info->edges[edge].highwater=0;
          polygon_info->edges[edge].ghostline=ghostline;
          polygon_info->edges[edge].direction=(ssize_t) (direction > 0);
          if (direction < 0)
            ReversePoints(points,(size_t) n);
          polygon_info->edges[edge].points=points;
          polygon_info->edges[edge].bounds=bounds;
          polygon_info->edges[edge].bounds.y1=points[0].y;
          polygon_info->edges[edge].bounds.y2=points[n-1].y;
          ghostline=MagickFalse;
          edge++;
        }
    }
  polygon_info->number_edges=edge;
  qsort(polygon_info->edges,(size_t) polygon_info->number_edges,
    sizeof(*polygon_info->edges),CompareEdges);
  if (IsEventLogging() != MagickFalse)
    LogPolygonInfo(polygon_info);
  return(polygon_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C o n v e r t P r i m i t i v e T o P a t h                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ConvertPrimitiveToPath() converts a PrimitiveInfo structure into a vector
%  path structure.
%
%  The format of the ConvertPrimitiveToPath method is:
%
%      PathInfo *ConvertPrimitiveToPath(const DrawInfo *draw_info,
%        const PrimitiveInfo *primitive_info)
%
%  A description of each parameter follows:
%
%    o Method ConvertPrimitiveToPath returns a vector path structure of type
%      PathInfo.
%
%    o draw_info: a structure of type DrawInfo.
%
%    o primitive_info: Specifies a pointer to an PrimitiveInfo structure.
%
%
*/

static void LogPathInfo(const PathInfo *path_info)
{
  register const PathInfo
    *p;

  (void) LogMagickEvent(DrawEvent,GetMagickModule(),"    begin vector-path");
  for (p=path_info; p->code != EndCode; p++)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),
      "      %g,%g %s",p->point.x,p->point.y,p->code == GhostlineCode ?
      "moveto ghostline" : p->code == OpenCode ? "moveto open" :
      p->code == MoveToCode ? "moveto" : p->code == LineToCode ? "lineto" :
      "?");
  (void) LogMagickEvent(DrawEvent,GetMagickModule(),"    end vector-path");
}

static PathInfo *ConvertPrimitiveToPath(
  const DrawInfo *magick_unused(draw_info),const PrimitiveInfo *primitive_info)
{
  MagickBooleanType
    closed_subpath;

  PathInfo
    *path_info;

  PathInfoCode
    code;

  PointInfo
    p,
    q;

  register ssize_t
    i,
    n;

  ssize_t
    coordinates,
    start;

  magick_unreferenced(draw_info);

  /*
    Converts a PrimitiveInfo structure into a vector path structure.
  */
  switch (primitive_info->primitive)
  {
    case PointPrimitive:
    case ColorPrimitive:
    case MattePrimitive:
    case TextPrimitive:
    case ImagePrimitive:
      return((PathInfo *) NULL);
    default:
      break;
  }
  for (i=0; primitive_info[i].primitive != UndefinedPrimitive; i++) ;
  path_info=(PathInfo *) AcquireQuantumMemory((size_t) (3UL*i+1UL),
    sizeof(*path_info));
  if (path_info == (PathInfo *) NULL)
    return((PathInfo *) NULL);
  coordinates=0;
  closed_subpath=MagickFalse;
  n=0;
  p.x=(-1.0);
  p.y=(-1.0);
  q.x=(-1.0);
  q.y=(-1.0);
  start=0;
  for (i=0; primitive_info[i].primitive != UndefinedPrimitive; i++)
  {
    code=LineToCode;
    if (coordinates <= 0)
      {
        /*
          New subpath.
        */
        coordinates=(ssize_t) primitive_info[i].coordinates;
        p=primitive_info[i].point;
        start=n;
        code=MoveToCode;
        closed_subpath=primitive_info[i].closed_subpath;
      }
    coordinates--;
    if ((code == MoveToCode) || (coordinates <= 0) ||
        (fabs(q.x-primitive_info[i].point.x) >= DrawEpsilon) ||
        (fabs(q.y-primitive_info[i].point.y) >= DrawEpsilon))
      {
        /*
          Eliminate duplicate points.
        */
        path_info[n].code=code;
        path_info[n].point=primitive_info[i].point;
        q=primitive_info[i].point;
        n++;
      }
    if (coordinates > 0)
      continue;  /* next point in current subpath */
    if (closed_subpath != MagickFalse)
      {
        closed_subpath=MagickFalse;
        continue;
      }
    /*
      Mark the p point as open if the subpath is not closed.
    */
    path_info[start].code=OpenCode;
    path_info[n].code=GhostlineCode;
    path_info[n].point=primitive_info[i].point;
    n++;
    path_info[n].code=LineToCode;
    path_info[n].point=p;
    n++;
  }
  path_info[n].code=EndCode;
  path_info[n].point.x=0.0;
  path_info[n].point.y=0.0;
  if (IsEventLogging() != MagickFalse)
    LogPathInfo(path_info);
  return(path_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   D e s t r o y D r a w I n f o                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DestroyDrawInfo() deallocates memory associated with an DrawInfo structure.
%
%  The format of the DestroyDrawInfo method is:
%
%      DrawInfo *DestroyDrawInfo(DrawInfo *draw_info)
%
%  A description of each parameter follows:
%
%    o draw_info: the draw info.
%
*/
MagickExport DrawInfo *DestroyDrawInfo(DrawInfo *draw_info)
{
  if (draw_info->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  assert(draw_info != (DrawInfo *) NULL);
  assert(draw_info->signature == MagickCoreSignature);
  if (draw_info->primitive != (char *) NULL)
    draw_info->primitive=DestroyString(draw_info->primitive);
  if (draw_info->text != (char *) NULL)
    draw_info->text=DestroyString(draw_info->text);
  if (draw_info->geometry != (char *) NULL)
    draw_info->geometry=DestroyString(draw_info->geometry);
  if (draw_info->tile != (Image *) NULL)
    draw_info->tile=DestroyImage(draw_info->tile);
  if (draw_info->fill_pattern != (Image *) NULL)
    draw_info->fill_pattern=DestroyImage(draw_info->fill_pattern);
  if (draw_info->stroke_pattern != (Image *) NULL)
    draw_info->stroke_pattern=DestroyImage(draw_info->stroke_pattern);
  if (draw_info->font != (char *) NULL)
    draw_info->font=DestroyString(draw_info->font);
  if (draw_info->metrics != (char *) NULL)
    draw_info->metrics=DestroyString(draw_info->metrics);
  if (draw_info->family != (char *) NULL)
    draw_info->family=DestroyString(draw_info->family);
  if (draw_info->encoding != (char *) NULL)
    draw_info->encoding=DestroyString(draw_info->encoding);
  if (draw_info->density != (char *) NULL)
    draw_info->density=DestroyString(draw_info->density);
  if (draw_info->server_name != (char *) NULL)
    draw_info->server_name=(char *)
     RelinquishMagickMemory(draw_info->server_name);
  if (draw_info->dash_pattern != (double *) NULL)
    draw_info->dash_pattern=(double *) RelinquishMagickMemory(
      draw_info->dash_pattern);
  if (draw_info->gradient.stops != (StopInfo *) NULL)
    draw_info->gradient.stops=(StopInfo *) RelinquishMagickMemory(
      draw_info->gradient.stops);
  if (draw_info->clip_mask != (char *) NULL)
    draw_info->clip_mask=DestroyString(draw_info->clip_mask);
  if (draw_info->clipping_mask != (Image *) NULL)
    draw_info->clipping_mask=DestroyImage(draw_info->clipping_mask);
  if (draw_info->composite_mask != (Image *) NULL)
    draw_info->composite_mask=DestroyImage(draw_info->composite_mask);
  draw_info->signature=(~MagickCoreSignature);
  draw_info=(DrawInfo *) RelinquishMagickMemory(draw_info);
  return(draw_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D e s t r o y E d g e                                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DestroyEdge() destroys the specified polygon edge.
%
%  The format of the DestroyEdge method is:
%
%      ssize_t DestroyEdge(PolygonInfo *polygon_info,const int edge)
%
%  A description of each parameter follows:
%
%    o polygon_info: Specifies a pointer to an PolygonInfo structure.
%
%    o edge: the polygon edge number to destroy.
%
*/
static size_t DestroyEdge(PolygonInfo *polygon_info,
  const size_t edge)
{
  assert(edge < polygon_info->number_edges);
  polygon_info->edges[edge].points=(PointInfo *) RelinquishMagickMemory(
    polygon_info->edges[edge].points);
  polygon_info->number_edges--;
  if (edge < polygon_info->number_edges)
    (void) memmove(polygon_info->edges+edge,polygon_info->edges+edge+1,
      (size_t) (polygon_info->number_edges-edge)*sizeof(*polygon_info->edges));
  return(polygon_info->number_edges);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D e s t r o y P o l y g o n I n f o                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DestroyPolygonInfo() destroys the PolygonInfo data structure.
%
%  The format of the DestroyPolygonInfo method is:
%
%      PolygonInfo *DestroyPolygonInfo(PolygonInfo *polygon_info)
%
%  A description of each parameter follows:
%
%    o polygon_info: Specifies a pointer to an PolygonInfo structure.
%
*/
static PolygonInfo *DestroyPolygonInfo(PolygonInfo *polygon_info)
{
  register ssize_t
    i;

  for (i=0; i < (ssize_t) polygon_info->number_edges; i++)
    polygon_info->edges[i].points=(PointInfo *)
      RelinquishMagickMemory(polygon_info->edges[i].points);
  polygon_info->edges=(EdgeInfo *) RelinquishMagickMemory(polygon_info->edges);
  return((PolygonInfo *) RelinquishMagickMemory(polygon_info));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     D r a w A f f i n e I m a g e                                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawAffineImage() composites the source over the destination image as
%  dictated by the affine transform.
%
%  The format of the DrawAffineImage method is:
%
%      MagickBooleanType DrawAffineImage(Image *image,const Image *source,
%        const AffineMatrix *affine)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o source: the source image.
%
%    o affine: the affine transform.
%
*/

static SegmentInfo AffineEdge(const Image *image,const AffineMatrix *affine,
  const double y,const SegmentInfo *edge)
{
  double
    intercept,
    z;

  register double
    x;

  SegmentInfo
    inverse_edge;

  /*
    Determine left and right edges.
  */
  inverse_edge.x1=edge->x1;
  inverse_edge.y1=edge->y1;
  inverse_edge.x2=edge->x2;
  inverse_edge.y2=edge->y2;
  z=affine->ry*y+affine->tx;
  if (affine->sx >= DrawEpsilon)
    {
      intercept=(-z/affine->sx);
      x=intercept;
      if (x > inverse_edge.x1)
        inverse_edge.x1=x;
      intercept=(-z+(double) image->columns)/affine->sx;
      x=intercept;
      if (x < inverse_edge.x2)
        inverse_edge.x2=x;
    }
  else
    if (affine->sx < -DrawEpsilon)
      {
        intercept=(-z+(double) image->columns)/affine->sx;
        x=intercept;
        if (x > inverse_edge.x1)
          inverse_edge.x1=x;
        intercept=(-z/affine->sx);
        x=intercept;
        if (x < inverse_edge.x2)
          inverse_edge.x2=x;
      }
    else
      if ((z < 0.0) || ((size_t) floor(z+0.5) >= image->columns))
        {
          inverse_edge.x2=edge->x1;
          return(inverse_edge);
        }
  /*
    Determine top and bottom edges.
  */
  z=affine->sy*y+affine->ty;
  if (affine->rx >= DrawEpsilon)
    {
      intercept=(-z/affine->rx);
      x=intercept;
      if (x > inverse_edge.x1)
        inverse_edge.x1=x;
      intercept=(-z+(double) image->rows)/affine->rx;
      x=intercept;
      if (x < inverse_edge.x2)
        inverse_edge.x2=x;
    }
  else
    if (affine->rx < -DrawEpsilon)
      {
        intercept=(-z+(double) image->rows)/affine->rx;
        x=intercept;
        if (x > inverse_edge.x1)
          inverse_edge.x1=x;
        intercept=(-z/affine->rx);
        x=intercept;
        if (x < inverse_edge.x2)
          inverse_edge.x2=x;
      }
    else
      if ((z < 0.0) || ((size_t) floor(z+0.5) >= image->rows))
        {
          inverse_edge.x2=edge->x2;
          return(inverse_edge);
        }
  return(inverse_edge);
}

static AffineMatrix InverseAffineMatrix(const AffineMatrix *affine)
{
  AffineMatrix
    inverse_affine;

  double
    determinant;

  determinant=PerceptibleReciprocal(affine->sx*affine->sy-affine->rx*
    affine->ry);
  inverse_affine.sx=determinant*affine->sy;
  inverse_affine.rx=determinant*(-affine->rx);
  inverse_affine.ry=determinant*(-affine->ry);
  inverse_affine.sy=determinant*affine->sx;
  inverse_affine.tx=(-affine->tx)*inverse_affine.sx-affine->ty*
    inverse_affine.ry;
  inverse_affine.ty=(-affine->tx)*inverse_affine.rx-affine->ty*
    inverse_affine.sy;
  return(inverse_affine);
}

MagickExport MagickBooleanType DrawAffineImage(Image *image,
  const Image *source,const AffineMatrix *affine)
{
  AffineMatrix
    inverse_affine;

  CacheView
    *image_view,
    *source_view;

  ExceptionInfo
    *exception;

  MagickBooleanType
    status;

  MagickPixelPacket
    zero;

  PointInfo
    extent[4],
    min,
    max,
    point;

  register ssize_t
    i;

  SegmentInfo
    edge;

  ssize_t
    start,
    stop,
    y;

  /*
    Determine bounding box.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(source != (const Image *) NULL);
  assert(source->signature == MagickCoreSignature);
  assert(affine != (AffineMatrix *) NULL);
  extent[0].x=0.0;
  extent[0].y=0.0;
  extent[1].x=(double) source->columns-1.0;
  extent[1].y=0.0;
  extent[2].x=(double) source->columns-1.0;
  extent[2].y=(double) source->rows-1.0;
  extent[3].x=0.0;
  extent[3].y=(double) source->rows-1.0;
  for (i=0; i < 4; i++)
  {
    point=extent[i];
    extent[i].x=point.x*affine->sx+point.y*affine->ry+affine->tx;
    extent[i].y=point.x*affine->rx+point.y*affine->sy+affine->ty;
  }
  min=extent[0];
  max=extent[0];
  for (i=1; i < 4; i++)
  {
    if (min.x > extent[i].x)
      min.x=extent[i].x;
    if (min.y > extent[i].y)
      min.y=extent[i].y;
    if (max.x < extent[i].x)
      max.x=extent[i].x;
    if (max.y < extent[i].y)
      max.y=extent[i].y;
  }
  /*
    Affine transform image.
  */
  if (SetImageStorageClass(image,DirectClass) == MagickFalse)
    return(MagickFalse);
  status=MagickTrue;
  edge.x1=MagickMax(min.x,0.0);
  edge.y1=MagickMax(min.y,0.0);
  edge.x2=MagickMin(max.x,(double) image->columns-1.0);
  edge.y2=MagickMin(max.y,(double) image->rows-1.0);
  inverse_affine=InverseAffineMatrix(affine);
  GetMagickPixelPacket(image,&zero);
  exception=(&image->exception);
  start=(ssize_t) ceil(edge.y1-0.5);
  stop=(ssize_t) floor(edge.y2+0.5);
  source_view=AcquireVirtualCacheView(source,exception);
  image_view=AcquireAuthenticCacheView(image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(status) \
    magick_number_threads(source,image,stop-start,1)
#endif
  for (y=start; y <= stop; y++)
  {
    MagickPixelPacket
      composite,
      pixel;

    PointInfo
      point;

    register IndexPacket
      *magick_restrict indexes;

    register ssize_t
      x;

    register PixelPacket
      *magick_restrict q;

    SegmentInfo
      inverse_edge;

    ssize_t
      x_offset;

    inverse_edge=AffineEdge(source,&inverse_affine,(double) y,&edge);
    if (inverse_edge.x2 < inverse_edge.x1)
      continue;
    q=GetCacheViewAuthenticPixels(image_view,(ssize_t) ceil(inverse_edge.x1-
      0.5),y,(size_t) (floor(inverse_edge.x2+0.5)-ceil(inverse_edge.x1-0.5)+1),
      1,exception);
    if (q == (PixelPacket *) NULL)
      continue;
    indexes=GetCacheViewAuthenticIndexQueue(image_view);
    pixel=zero;
    composite=zero;
    x_offset=0;
    for (x=(ssize_t) ceil(inverse_edge.x1-0.5); x <= (ssize_t) floor(inverse_edge.x2+0.5); x++)
    {
      point.x=(double) x*inverse_affine.sx+y*inverse_affine.ry+
        inverse_affine.tx;
      point.y=(double) x*inverse_affine.rx+y*inverse_affine.sy+
        inverse_affine.ty;
      status=InterpolateMagickPixelPacket(source,source_view,
        UndefinedInterpolatePixel,point.x,point.y,&pixel,exception);
      if (status == MagickFalse)
        break;
      SetMagickPixelPacket(image,q,indexes+x_offset,&composite);
      MagickPixelCompositeOver(&pixel,pixel.opacity,&composite,
        composite.opacity,&composite);
      SetPixelPacket(image,&composite,q,indexes+x_offset);
      x_offset++;
      q++;
    }
    if (SyncCacheViewAuthenticPixels(image_view,exception) == MagickFalse)
      status=MagickFalse;
  }
  source_view=DestroyCacheView(source_view);
  image_view=DestroyCacheView(image_view);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D r a w B o u n d i n g R e c t a n g l e s                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawBoundingRectangles() draws the bounding rectangles on the image.  This
%  is only useful for developers debugging the rendering algorithm.
%
%  The format of the DrawBoundingRectangles method is:
%
%      void DrawBoundingRectangles(Image *image,const DrawInfo *draw_info,
%        PolygonInfo *polygon_info)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o polygon_info: Specifies a pointer to a PolygonInfo structure.
%
*/

static inline double SaneStrokeWidth(const Image *image,
  const DrawInfo *draw_info)
{
  return(MagickMin((double) draw_info->stroke_width,
    (2.0*sqrt(2.0)+DrawEpsilon)*MagickMax(image->columns,image->rows)));
}

static void DrawBoundingRectangles(Image *image,const DrawInfo *draw_info,
  const PolygonInfo *polygon_info)
{
  double
    mid;

  DrawInfo
    *clone_info;

  PointInfo
    end,
    resolution,
    start;

  PrimitiveInfo
    primitive_info[6];

  register ssize_t
    i;

  SegmentInfo
    bounds;

  ssize_t
    coordinates;

  (void) memset(primitive_info,0,sizeof(primitive_info));
  clone_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  (void) QueryColorDatabase("#0000",&clone_info->fill,&image->exception);
  resolution.x=96.0;
  resolution.y=96.0;
  if (clone_info->density != (char *) NULL)
    {
      GeometryInfo
        geometry_info;

      MagickStatusType
        flags;

      flags=ParseGeometry(clone_info->density,&geometry_info);
      resolution.x=geometry_info.rho;
      resolution.y=geometry_info.sigma;
      if ((flags & SigmaValue) == MagickFalse)
        resolution.y=resolution.x;
    }
  mid=(resolution.x/96.0)*ExpandAffine(&clone_info->affine)*
    SaneStrokeWidth(image,clone_info)/2.0;
  bounds.x1=0.0;
  bounds.y1=0.0;
  bounds.x2=0.0;
  bounds.y2=0.0;
  if (polygon_info != (PolygonInfo *) NULL)
    {
      bounds=polygon_info->edges[0].bounds;
      for (i=1; i < (ssize_t) polygon_info->number_edges; i++)
      {
        if (polygon_info->edges[i].bounds.x1 < (double) bounds.x1)
          bounds.x1=polygon_info->edges[i].bounds.x1;
        if (polygon_info->edges[i].bounds.y1 < (double) bounds.y1)
          bounds.y1=polygon_info->edges[i].bounds.y1;
        if (polygon_info->edges[i].bounds.x2 > (double) bounds.x2)
          bounds.x2=polygon_info->edges[i].bounds.x2;
        if (polygon_info->edges[i].bounds.y2 > (double) bounds.y2)
          bounds.y2=polygon_info->edges[i].bounds.y2;
      }
      bounds.x1-=mid;
      bounds.x1=bounds.x1 < 0.0 ? 0.0 : bounds.x1 >= (double)
        image->columns ? (double) image->columns-1 : bounds.x1;
      bounds.y1-=mid;
      bounds.y1=bounds.y1 < 0.0 ? 0.0 : bounds.y1 >= (double)
        image->rows ? (double) image->rows-1 : bounds.y1;
      bounds.x2+=mid;
      bounds.x2=bounds.x2 < 0.0 ? 0.0 : bounds.x2 >= (double)
        image->columns ? (double) image->columns-1 : bounds.x2;
      bounds.y2+=mid;
      bounds.y2=bounds.y2 < 0.0 ? 0.0 : bounds.y2 >= (double)
        image->rows ? (double) image->rows-1 : bounds.y2;
      for (i=0; i < (ssize_t) polygon_info->number_edges; i++)
      {
        if (polygon_info->edges[i].direction != 0)
          (void) QueryColorDatabase("red",&clone_info->stroke,
            &image->exception);
        else
          (void) QueryColorDatabase("green",&clone_info->stroke,
            &image->exception);
        start.x=(double) (polygon_info->edges[i].bounds.x1-mid);
        start.y=(double) (polygon_info->edges[i].bounds.y1-mid);
        end.x=(double) (polygon_info->edges[i].bounds.x2+mid);
        end.y=(double) (polygon_info->edges[i].bounds.y2+mid);
        primitive_info[0].primitive=RectanglePrimitive;
        TraceRectangle(primitive_info,start,end);
        primitive_info[0].method=ReplaceMethod;
        coordinates=(ssize_t) primitive_info[0].coordinates;
        primitive_info[coordinates].primitive=UndefinedPrimitive;
        (void) DrawPrimitive(image,clone_info,primitive_info);
      }
    }
  (void) QueryColorDatabase("blue",&clone_info->stroke,&image->exception);
  start.x=(double) (bounds.x1-mid);
  start.y=(double) (bounds.y1-mid);
  end.x=(double) (bounds.x2+mid);
  end.y=(double) (bounds.y2+mid);
  primitive_info[0].primitive=RectanglePrimitive;
  TraceRectangle(primitive_info,start,end);
  primitive_info[0].method=ReplaceMethod;
  coordinates=(ssize_t) primitive_info[0].coordinates;
  primitive_info[coordinates].primitive=UndefinedPrimitive;
  (void) DrawPrimitive(image,clone_info,primitive_info);
  clone_info=DestroyDrawInfo(clone_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   D r a w C l i p P a t h                                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawClipPath() draws the clip path on the image mask.
%
%  The format of the DrawClipPath method is:
%
%      MagickBooleanType DrawClipPath(Image *image,const DrawInfo *draw_info,
%        const char *id)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o id: the id of the clip path.
%
*/
static char *GetNodeByURL(const char *,const char *);
MagickExport MagickBooleanType DrawClipPath(Image *image,
  const DrawInfo *draw_info,const char *id)
{
  const char
    *clip_path;

  Image
    *clipping_mask;

  MagickBooleanType
    status;

  clip_path=GetImageArtifact(image,id);
  if (clip_path == (const char *) NULL)
    return(MagickFalse);
  clipping_mask=DrawClippingMask(image,draw_info,draw_info->clip_mask,clip_path,
    &image->exception);
  if (clipping_mask == (Image *) NULL)
    return(MagickFalse);
  status=SetImageClipMask(image,clipping_mask);
  clipping_mask=DestroyImage(clipping_mask);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   D r a w C l i p p i n g M a s k                                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawClippingMask() draws the clip path and returns it as an image clipping
%  mask.
%
%  The format of the DrawClippingMask method is:
%
%      Image *DrawClippingMask(Image *image,const DrawInfo *draw_info,
%        const char *id,const char *clip_path,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o id: the clip path id.
%
%    o clip_path: the clip path.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static Image *DrawClippingMask(Image *image,const DrawInfo *draw_info,
  const char *id,const char *clip_path,ExceptionInfo *exception)
{
  DrawInfo
    *clone_info;

  Image
    *clip_mask;

  MagickStatusType
    status;

  /*
    Draw a clip path.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(draw_info != (const DrawInfo *) NULL);
  clip_mask=CloneImage(image,image->columns,image->rows,MagickTrue,exception);
  if (clip_mask == (Image *) NULL)
    return((Image *) NULL);
  (void) SetImageClipMask(image,(Image *) NULL);
  (void) QueryColorCompliance("#0000",AllCompliance,
    &clip_mask->background_color,exception);
  clip_mask->background_color.opacity=(Quantum) TransparentOpacity;
  (void) SetImageBackgroundColor(clip_mask);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"\nbegin clip-path %s",
      id);
  clone_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  (void) CloneString(&clone_info->primitive,clip_path);
  (void) QueryColorCompliance("#ffffff",AllCompliance,&clone_info->fill,
    exception);
  if (clone_info->clip_mask != (char *) NULL)
    clone_info->clip_mask=DestroyString(clone_info->clip_mask);
  (void) QueryColorCompliance("#00000000",AllCompliance,&clone_info->stroke,
    exception);
  clone_info->stroke_width=0.0;
  clone_info->opacity=OpaqueOpacity;
  clone_info->clip_path=MagickTrue;
  status=DrawImage(clip_mask,clone_info);
  clone_info=DestroyDrawInfo(clone_info);
  status&=SeparateImageChannel(clip_mask,TrueAlphaChannel);
  status&=NegateImage(clip_mask,MagickFalse);
  if (status == MagickFalse)
    clip_mask=DestroyImage(clip_mask);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"end clip-path");
  return(clip_mask);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   D r a w C o m p o s i t e M a s k                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawCompositeMask() draws the mask path and returns it as an image mask.
%
%  The format of the DrawCompositeMask method is:
%
%      Image *DrawCompositeMask(Image *image,const DrawInfo *draw_info,
%        const char *id,const char *mask_path,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o id: the mask path id.
%
%    o mask_path: the mask path.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static Image *DrawCompositeMask(Image *image,const DrawInfo *draw_info,
  const char *id,const char *mask_path,ExceptionInfo *exception)
{
  Image
    *composite_mask;

  DrawInfo
    *clone_info;

  MagickStatusType
    status;

  /*
    Draw a mask path.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(draw_info != (const DrawInfo *) NULL);
  composite_mask=CloneImage(image,image->columns,image->rows,MagickTrue,
    exception);
  if (composite_mask == (Image *) NULL)
    return((Image *) NULL);
  (void) SetImageMask(image,(Image *) NULL);
  (void) QueryColorCompliance("#0000",AllCompliance,
    &composite_mask->background_color,exception);
  composite_mask->background_color.opacity=(Quantum) TransparentOpacity;
  (void) SetImageBackgroundColor(composite_mask);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"\nbegin mask-path %s",
      id);
  clone_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  (void) CloneString(&clone_info->primitive,mask_path);
  (void) QueryColorCompliance("#ffffff",AllCompliance,&clone_info->fill,
    exception);
  (void) QueryColorCompliance("#00000000",AllCompliance,&clone_info->stroke,
    exception);
  clone_info->stroke_width=0.0;
  clone_info->opacity=OpaqueOpacity;
  status=DrawImage(composite_mask,clone_info);
  clone_info=DestroyDrawInfo(clone_info);
  status&=SeparateImageChannel(composite_mask,TrueAlphaChannel);
  status&=NegateImage(composite_mask,MagickFalse);
  if (status == MagickFalse)
    composite_mask=DestroyImage(composite_mask);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"end mask-path");
  return(composite_mask);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D r a w D a s h P o l y g o n                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawDashPolygon() draws a dashed polygon (line, rectangle, ellipse) on the
%  image while respecting the dash offset and dash pattern attributes.
%
%  The format of the DrawDashPolygon method is:
%
%      MagickBooleanType DrawDashPolygon(const DrawInfo *draw_info,
%        const PrimitiveInfo *primitive_info,Image *image)
%
%  A description of each parameter follows:
%
%    o draw_info: the draw info.
%
%    o primitive_info: Specifies a pointer to a PrimitiveInfo structure.
%
%    o image: the image.
%
%
*/
static MagickBooleanType DrawDashPolygon(const DrawInfo *draw_info,
  const PrimitiveInfo *primitive_info,Image *image)
{
  double
    length,
    maximum_length,
    offset,
    scale,
    total_length;

  DrawInfo
    *clone_info;

  MagickStatusType
    status;

  PrimitiveInfo
    *dash_polygon;

  register double
    dx,
    dy;

  register ssize_t
    i;

  size_t
    number_vertices;

  ssize_t
    j,
    n;

  assert(draw_info != (const DrawInfo *) NULL);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"    begin draw-dash");
  for (i=0; primitive_info[i].primitive != UndefinedPrimitive; i++) ;
  number_vertices=(size_t) i;
  dash_polygon=(PrimitiveInfo *) AcquireQuantumMemory((size_t)
    (2UL*number_vertices+32UL),sizeof(*dash_polygon));
  if (dash_polygon == (PrimitiveInfo *) NULL)
    return(MagickFalse);
  (void) memset(dash_polygon,0,(2UL*number_vertices+32UL)*
    sizeof(*dash_polygon));
  clone_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  clone_info->miterlimit=0;
  dash_polygon[0]=primitive_info[0];
  scale=ExpandAffine(&draw_info->affine);
  length=scale*(draw_info->dash_pattern[0]-0.5);
  offset=fabs(draw_info->dash_offset) >= DrawEpsilon ?
    scale*draw_info->dash_offset : 0.0;
  j=1;
  for (n=0; offset > 0.0; j=0)
  {
    if (draw_info->dash_pattern[n] <= 0.0)
      break;
    length=scale*(draw_info->dash_pattern[n]+(n == 0 ? -0.5 : 0.5));
    if (offset > length)
      {
        offset-=length;
        n++;
        length=scale*(draw_info->dash_pattern[n]+0.5);
        continue;
      }
    if (offset < length)
      {
        length-=offset;
        offset=0.0;
        break;
      }
    offset=0.0;
    n++;
  }
  status=MagickTrue;
  maximum_length=0.0;
  total_length=0.0;
  for (i=1; (i < (ssize_t) number_vertices) && (length >= 0.0); i++)
  {
    dx=primitive_info[i].point.x-primitive_info[i-1].point.x;
    dy=primitive_info[i].point.y-primitive_info[i-1].point.y;
    maximum_length=hypot(dx,dy);
    if (maximum_length > MaxBezierCoordinates)
      break;
    if (fabs(length) < DrawEpsilon)
      {
        n++;
        if (fabs(draw_info->dash_pattern[n]) < DrawEpsilon)
          n=0;
        length=scale*(draw_info->dash_pattern[n]+(n == 0 ? -0.5 : 0.5));
      }
    for (total_length=0.0; (length >= 0.0) && (maximum_length >= (total_length+length)); )
    {
      total_length+=length;
      if ((n & 0x01) != 0)
        {
          dash_polygon[0]=primitive_info[0];
          dash_polygon[0].point.x=(double) (primitive_info[i-1].point.x+dx*
            total_length*PerceptibleReciprocal(maximum_length));
          dash_polygon[0].point.y=(double) (primitive_info[i-1].point.y+dy*
            total_length*PerceptibleReciprocal(maximum_length));
          j=1;
        }
      else
        {
          if ((j+1) > (ssize_t) (2*number_vertices))
            break;
          dash_polygon[j]=primitive_info[i-1];
          dash_polygon[j].point.x=(double) (primitive_info[i-1].point.x+dx*
            total_length*PerceptibleReciprocal(maximum_length));
          dash_polygon[j].point.y=(double) (primitive_info[i-1].point.y+dy*
            total_length*PerceptibleReciprocal(maximum_length));
          dash_polygon[j].coordinates=1;
          j++;
          dash_polygon[0].coordinates=(size_t) j;
          dash_polygon[j].primitive=UndefinedPrimitive;
          status&=DrawStrokePolygon(image,clone_info,dash_polygon);
        }
      n++;
      if (fabs(draw_info->dash_pattern[n]) < DrawEpsilon)
        n=0;
      length=scale*(draw_info->dash_pattern[n]+(n == 0 ? -0.5 : 0.5));
    }
    length-=(maximum_length-total_length);
    if ((n & 0x01) != 0)
      continue;
    dash_polygon[j]=primitive_info[i];
    dash_polygon[j].coordinates=1;
    j++;
  }
  if ((total_length <= maximum_length) && ((n & 0x01) == 0) && (j > 1))
    {
      dash_polygon[j]=primitive_info[i-1];
      dash_polygon[j].point.x+=DrawEpsilon;
      dash_polygon[j].point.y+=DrawEpsilon;
      dash_polygon[j].coordinates=1;
      j++;
      dash_polygon[0].coordinates=(size_t) j;
      dash_polygon[j].primitive=UndefinedPrimitive;
      status&=DrawStrokePolygon(image,clone_info,dash_polygon);
    }
  dash_polygon=(PrimitiveInfo *) RelinquishMagickMemory(dash_polygon);
  clone_info=DestroyDrawInfo(clone_info);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"    end draw-dash");
  return(status != 0 ? MagickTrue : MagickFalse);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     D r a w G r a d i e n t I m a g e                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawGradientImage() draws a linear gradient on the image.
%
%  The format of the DrawGradientImage method is:
%
%      MagickBooleanType DrawGradientImage(Image *image,
%        const DrawInfo *draw_info)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o _info: the draw info.
%
*/

static inline double GetStopColorOffset(const GradientInfo *gradient,
  const ssize_t x,const ssize_t y)
{
  switch (gradient->type)
  {
    case UndefinedGradient:
    case LinearGradient:
    {
      double
        gamma,
        length,
        offset,
        scale;

      PointInfo
        p,
        q;

      const SegmentInfo
        *gradient_vector;

      gradient_vector=(&gradient->gradient_vector);
      p.x=gradient_vector->x2-gradient_vector->x1;
      p.y=gradient_vector->y2-gradient_vector->y1;
      q.x=(double) x-gradient_vector->x1;
      q.y=(double) y-gradient_vector->y1;
      length=sqrt(q.x*q.x+q.y*q.y);
      gamma=sqrt(p.x*p.x+p.y*p.y)*length;
      gamma=PerceptibleReciprocal(gamma);
      scale=p.x*q.x+p.y*q.y;
      offset=gamma*scale*length;
      return(offset);
    }
    case RadialGradient:
    {
      PointInfo
        v;

      if (gradient->spread == RepeatSpread)
        {
          v.x=(double) x-gradient->center.x;
          v.y=(double) y-gradient->center.y;
          return(sqrt(v.x*v.x+v.y*v.y));
        }
      v.x=(double) (((x-gradient->center.x)*cos(DegreesToRadians(
        gradient->angle)))+((y-gradient->center.y)*sin(DegreesToRadians(
        gradient->angle))))*PerceptibleReciprocal(gradient->radii.x);
      v.y=(double) (((x-gradient->center.x)*sin(DegreesToRadians(
        gradient->angle)))-((y-gradient->center.y)*cos(DegreesToRadians(
        gradient->angle))))*PerceptibleReciprocal(gradient->radii.y);
      return(sqrt(v.x*v.x+v.y*v.y));
    }
  }
  return(0.0);
}

MagickExport MagickBooleanType DrawGradientImage(Image *image,
  const DrawInfo *draw_info)
{
  CacheView
    *image_view;

  const GradientInfo
    *gradient;

  const SegmentInfo
    *gradient_vector;

  double
    length;

  ExceptionInfo
    *exception;

  MagickBooleanType
    status;

  MagickPixelPacket
    zero;

  PointInfo
    point;

  RectangleInfo
    bounding_box;

  ssize_t
    y;

  /*
    Draw linear or radial gradient on image.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(draw_info != (const DrawInfo *) NULL);
  gradient=(&draw_info->gradient);
  gradient_vector=(&gradient->gradient_vector);
  point.x=gradient_vector->x2-gradient_vector->x1;
  point.y=gradient_vector->y2-gradient_vector->y1;
  length=sqrt(point.x*point.x+point.y*point.y);
  bounding_box=gradient->bounding_box;
  status=MagickTrue;
  exception=(&image->exception);
  GetMagickPixelPacket(image,&zero);
  image_view=AcquireAuthenticCacheView(image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(status) \
    magick_number_threads(image,image,bounding_box.height-bounding_box.y,1)
#endif
  for (y=bounding_box.y; y < (ssize_t) bounding_box.height; y++)
  {
    double
      alpha,
      offset;

    MagickPixelPacket
      composite,
      pixel;

    register IndexPacket
      *magick_restrict indexes;

    register ssize_t
      i,
      x;

    register PixelPacket
      *magick_restrict q;

    ssize_t
      j;

    if (status == MagickFalse)
      continue;
    q=GetCacheViewAuthenticPixels(image_view,0,y,image->columns,1,exception);
    if (q == (PixelPacket *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    indexes=GetCacheViewAuthenticIndexQueue(image_view);
    pixel=zero;
    composite=zero;
    offset=GetStopColorOffset(gradient,0,y);
    if (gradient->type != RadialGradient)
      offset*=PerceptibleReciprocal(length);
    for (x=bounding_box.x; x < (ssize_t) bounding_box.width; x++)
    {
      SetMagickPixelPacket(image,q,indexes+x,&pixel);
      switch (gradient->spread)
      {
        case UndefinedSpread:
        case PadSpread:
        {
          if ((x != (ssize_t) ceil(gradient_vector->x1-0.5)) ||
              (y != (ssize_t) ceil(gradient_vector->y1-0.5)))
            {
              offset=GetStopColorOffset(gradient,x,y);
              if (gradient->type != RadialGradient)
                offset*=PerceptibleReciprocal(length);
            }
          for (i=0; i < (ssize_t) gradient->number_stops; i++)
            if (offset < gradient->stops[i].offset)
              break;
          if ((offset < 0.0) || (i == 0))
            composite=gradient->stops[0].color;
          else
            if ((offset > 1.0) || (i == (ssize_t) gradient->number_stops))
              composite=gradient->stops[gradient->number_stops-1].color;
            else
              {
                j=i;
                i--;
                alpha=(offset-gradient->stops[i].offset)/
                  (gradient->stops[j].offset-gradient->stops[i].offset);
                MagickPixelCompositeBlend(&gradient->stops[i].color,1.0-alpha,
                  &gradient->stops[j].color,alpha,&composite);
              }
          break;
        }
        case ReflectSpread:
        {
          if ((x != (ssize_t) ceil(gradient_vector->x1-0.5)) ||
              (y != (ssize_t) ceil(gradient_vector->y1-0.5)))
            {
              offset=GetStopColorOffset(gradient,x,y);
              if (gradient->type != RadialGradient)
                offset*=PerceptibleReciprocal(length);
            }
          if (offset < 0.0)
            offset=(-offset);
          if ((ssize_t) fmod(offset,2.0) == 0)
            offset=fmod(offset,1.0);
          else
            offset=1.0-fmod(offset,1.0);
          for (i=0; i < (ssize_t) gradient->number_stops; i++)
            if (offset < gradient->stops[i].offset)
              break;
          if (i == 0)
            composite=gradient->stops[0].color;
          else
            if (i == (ssize_t) gradient->number_stops)
              composite=gradient->stops[gradient->number_stops-1].color;
            else
              {
                j=i;
                i--;
                alpha=(offset-gradient->stops[i].offset)/
                  (gradient->stops[j].offset-gradient->stops[i].offset);
                MagickPixelCompositeBlend(&gradient->stops[i].color,1.0-alpha,
                  &gradient->stops[j].color,alpha,&composite);
              }
          break;
        }
        case RepeatSpread:
        {
          double
            repeat;

          MagickBooleanType
            antialias;

          antialias=MagickFalse;
          repeat=0.0;
          if ((x != (ssize_t) ceil(gradient_vector->x1-0.5)) ||
              (y != (ssize_t) ceil(gradient_vector->y1-0.5)))
            {
              offset=GetStopColorOffset(gradient,x,y);
              if (gradient->type == LinearGradient)
                {
                  repeat=fmod(offset,length);
                  if (repeat < 0.0)
                    repeat=length-fmod(-repeat,length);
                  else
                    repeat=fmod(offset,length);
                  antialias=(repeat < length) && ((repeat+1.0) > length) ?
                    MagickTrue : MagickFalse;
									offset=PerceptibleReciprocal(length)*repeat;
                }
              else
                {
                  repeat=fmod(offset,(double) gradient->radius);
                  if (repeat < 0.0)
                    repeat=gradient->radius-fmod(-repeat,
                      (double) gradient->radius);
                  else
                    repeat=fmod(offset,(double) gradient->radius);
                  antialias=repeat+1.0 > gradient->radius ? MagickTrue :
                    MagickFalse;
                  offset=repeat/gradient->radius;
                }
            }
          for (i=0; i < (ssize_t) gradient->number_stops; i++)
            if (offset < gradient->stops[i].offset)
              break;
          if (i == 0)
            composite=gradient->stops[0].color;
          else
            if (i == (ssize_t) gradient->number_stops)
              composite=gradient->stops[gradient->number_stops-1].color;
            else
              {
                j=i;
                i--;
                alpha=(offset-gradient->stops[i].offset)/
                  (gradient->stops[j].offset-gradient->stops[i].offset);
                if (antialias != MagickFalse)
                  {
                    if (gradient->type == LinearGradient)
                      alpha=length-repeat;
                    else
                      alpha=gradient->radius-repeat;
                    i=0;
                    j=(ssize_t) gradient->number_stops-1L;
                  }
                MagickPixelCompositeBlend(&gradient->stops[i].color,1.0-alpha,
                  &gradient->stops[j].color,alpha,&composite);
              }
          break;
        }
      }
      MagickPixelCompositeOver(&composite,composite.opacity,&pixel,
        pixel.opacity,&pixel);
      SetPixelPacket(image,&pixel,q,indexes+x);
      q++;
    }
    if (SyncCacheViewAuthenticPixels(image_view,exception) == MagickFalse)
      status=MagickFalse;
  }
  image_view=DestroyCacheView(image_view);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   D r a w I m a g e                                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawImage() draws a graphic primitive on your image.  The primitive
%  may be represented as a string or filename.  Precede the filename with an
%  "at" sign (@) and the contents of the file are drawn on the image.  You
%  can affect how text is drawn by setting one or more members of the draw
%  info structure.
%
%  The format of the DrawImage method is:
%
%      MagickBooleanType DrawImage(Image *image,const DrawInfo *draw_info)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
*/

static char *GetNodeByURL(const char *primitive,const char *url)
{
  char
    *token;

  const char
    *q,
    *start;

  register const char
    *p;

  size_t
    extent,
    length;

  ssize_t
    n;

  /*
    Find and return node by ID.
  */
  if (primitive == (const char *) NULL)
    return((char *) NULL);
  token=AcquireString(primitive);
  extent=strlen(token)+MagickPathExtent;
  length=0;
  n=0;
  start=(const char *) NULL;
  p=(const char *) NULL;
  for (q=primitive; (*q != '\0') && (length == 0); )
  {
    p=q;
    GetNextToken(q,&q,extent,token);
    if (*token == '\0')
      break;
    if (*token == '#')
      {
        /*
          Comment.
        */
        while ((*q != '\n') && (*q != '\0'))
          q++;
        continue;
      }
    if (LocaleCompare("pop",token) == 0)
      {
        GetNextToken(q,&q,extent,token);
        if ((n == 0) && (start != (const char *) NULL))
          {
            /*
              End of node by ID.
            */
            length=(size_t) (p-start+1);
            break;
          }
        n--;
      }
    if (LocaleCompare("push",token) == 0)
      {
        GetNextToken(q,&q,extent,token);
        n++;
        if (*q == '"')
          {
            GetNextToken(q,&q,extent,token);
            if (LocaleCompare(url,token) == 0)
              {
                /*
                  Start of node by ID.
                */
                n=0;
                start=q;
              }
          }
      }
  }
  if (start == (const char *) NULL)
    return(DestroyString(token));
  (void) CopyMagickString(token,start,length);
  return(token);
}

static size_t ReckonEllipseCoordinates(const PointInfo radii,
  const PointInfo arc)
{
  double
    delta,
    step,
    y;

  PointInfo
    angle;

  /*
    Ellipses are just short segmented polys.
  */
  if ((fabs(radii.x) < DrawEpsilon) || (fabs(radii.y) < DrawEpsilon))
    return(0);
  delta=2.0*PerceptibleReciprocal(MagickMax(radii.x,radii.y));
  step=MagickPI/8.0;
  if ((delta >= 0.0) && (delta < (MagickPI/8.0)))
    step=MagickPI/(4.0*(MagickPI*PerceptibleReciprocal(delta)/2.0));
  angle.x=DegreesToRadians(arc.x);
  y=arc.y;
  while (y < arc.x)
    y+=360.0;
  angle.y=DegreesToRadians(y);
  return((size_t) floor((angle.y-angle.x)/step+0.5)+3);
}

static inline MagickBooleanType IsPoint(const char *point)
{
  char
    *p;

  double
    value;

  value=StringToDouble(point,&p);
  return((fabs(value) < DrawEpsilon) && (p == point) ? MagickFalse : MagickTrue);
}

static size_t ReckonRoundRectangleCoordinates(const PointInfo start,
  const PointInfo end,PointInfo arc)
{
  PointInfo
    degrees,
    offset;

  size_t
    coordinates;

  offset.x=fabs(end.x-start.x);
  offset.y=fabs(end.y-start.y);
  if ((offset.x < DrawEpsilon) || (offset.y < DrawEpsilon))
    return(0);
  coordinates=0;
  if (arc.x > (0.5*offset.x))
    arc.x=0.5*offset.x;
  if (arc.y > (0.5*offset.y))
    arc.y=0.5*offset.y;
  degrees.x=270.0;
  degrees.y=360.0;
  coordinates+=ReckonEllipseCoordinates(arc,degrees);
  degrees.x=0.0;
  degrees.y=90.0;
  coordinates+=ReckonEllipseCoordinates(arc,degrees);
  degrees.x=90.0;
  degrees.y=180.0;
  coordinates+=ReckonEllipseCoordinates(arc,degrees);
  degrees.x=180.0;
  degrees.y=270.0;
  coordinates+=ReckonEllipseCoordinates(arc,degrees);
  return(coordinates+1);
}

static inline void TracePoint(PrimitiveInfo *primitive_info,
  const PointInfo point)
{
  primitive_info->coordinates=1;
  primitive_info->closed_subpath=MagickFalse;
  primitive_info->point=point;
}

MagickExport MagickBooleanType DrawImage(Image *image,const DrawInfo *draw_info)
{
#define RenderImageTag  "Render/Image"

  AffineMatrix
    affine,
    current;

  char
    key[2*MaxTextExtent],
    keyword[MaxTextExtent],
    geometry[MaxTextExtent],
    name[MaxTextExtent],
    *next_token,
    pattern[MaxTextExtent],
    *primitive,
    *token;

  const char
    *q;

  double
    angle,
    factor,
    primitive_extent;

  DrawInfo
    **graphic_context;

  MagickBooleanType
    proceed;

  MagickSizeType
    number_points;

  MagickStatusType
    status;

  PointInfo
    point;

  PixelPacket
    start_color;

  PrimitiveInfo
    *primitive_info;

  PrimitiveType
    primitive_type;

  register const char
    *p;

  register ssize_t
    i,
    x;

  SegmentInfo
    bounds;

  size_t
    coordinates,
    extent;

  ssize_t
    defsDepth,
    j,
    k,
    n,
    symbolDepth;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(draw_info != (DrawInfo *) NULL);
  assert(draw_info->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  if ((draw_info->primitive == (char *) NULL) ||
      (*draw_info->primitive == '\0'))
    return(MagickFalse);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"begin draw-image");
  if (SetImageStorageClass(image,DirectClass) == MagickFalse)
    return(MagickFalse);
  if (image->matte == MagickFalse)
    {
      status=SetImageAlphaChannel(image,OpaqueAlphaChannel);
      if (status == MagickFalse)
        return(status);
    }
  primitive=(char *) NULL;
  if (*draw_info->primitive != '@')
    primitive=AcquireString(draw_info->primitive);
  else
    if ((strlen(draw_info->primitive) > 1) &&
        (*(draw_info->primitive+1) != '-'))
      primitive=FileToString(draw_info->primitive+1,~0UL,&image->exception);
  if (primitive == (char *) NULL)
    return(MagickFalse);
  primitive_extent=(double) strlen(primitive);
  (void) SetImageArtifact(image,"MVG",primitive);
  n=0;
  /*
    Allocate primitive info memory.
  */
  graphic_context=(DrawInfo **) AcquireMagickMemory(sizeof(*graphic_context));
  if (graphic_context == (DrawInfo **) NULL)
    {
      primitive=DestroyString(primitive);
      ThrowBinaryException(ResourceLimitError,"MemoryAllocationFailed",
        image->filename);
    }
  number_points=4096;
  primitive_info=(PrimitiveInfo *) AcquireQuantumMemory((size_t) number_points,
    sizeof(*primitive_info));
  if (primitive_info == (PrimitiveInfo *) NULL)
    {
      primitive=DestroyString(primitive);
      for ( ; n >= 0; n--)
        graphic_context[n]=DestroyDrawInfo(graphic_context[n]);
      graphic_context=(DrawInfo **) RelinquishMagickMemory(graphic_context);
      ThrowBinaryException(ResourceLimitError,"MemoryAllocationFailed",
        image->filename);
    }
  (void) memset(primitive_info,0,(size_t) number_points*
    sizeof(*primitive_info));
  graphic_context[n]=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  graphic_context[n]->viewbox=image->page;
  if ((image->page.width == 0) || (image->page.height == 0))
    {
      graphic_context[n]->viewbox.width=image->columns;
      graphic_context[n]->viewbox.height=image->rows;
    }
  token=AcquireString(primitive);
  extent=strlen(token)+MaxTextExtent;
  (void) QueryColorDatabase("#000000",&start_color,&image->exception);
  defsDepth=0;
  symbolDepth=0;
  status=MagickTrue;
  for (q=primitive; *q != '\0'; )
  {
    /*
      Interpret graphic primitive.
    */
    GetNextToken(q,&q,MaxTextExtent,keyword);
    if (*keyword == '\0')
      break;
    if (*keyword == '#')
      {
        /*
          Comment.
        */
        while ((*q != '\n') && (*q != '\0'))
          q++;
        continue;
      }
    p=q-strlen(keyword)-1;
    primitive_type=UndefinedPrimitive;
    current=graphic_context[n]->affine;
    GetAffineMatrix(&affine);
    switch (*keyword)
    {
      case ';':
        break;
      case 'a':
      case 'A':
      {
        if (LocaleCompare("affine",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            affine.sx=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(q,&q,extent,token);
            if (*token == ',')
              GetNextToken(q,&q,extent,token);
            affine.rx=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(q,&q,extent,token);
            if (*token == ',')
              GetNextToken(q,&q,extent,token);
            affine.ry=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(q,&q,extent,token);
            if (*token == ',')
              GetNextToken(q,&q,extent,token);
            affine.sy=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(q,&q,extent,token);
            if (*token == ',')
              GetNextToken(q,&q,extent,token);
            affine.tx=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(q,&q,extent,token);
            if (*token == ',')
              GetNextToken(q,&q,extent,token);
            affine.ty=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        if (LocaleCompare("arc",keyword) == 0)
          {
            primitive_type=ArcPrimitive;
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'b':
      case 'B':
      {
        if (LocaleCompare("bezier",keyword) == 0)
          {
            primitive_type=BezierPrimitive;
            break;
          }
        if (LocaleCompare("border-color",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            status&=QueryColorDatabase(token,&graphic_context[n]->border_color,
              &image->exception);
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'c':
      case 'C':
      {
        if (LocaleCompare("clip-path",keyword) == 0)
          {
            char
              *clip_path;

            /*
              Take a node from within the MVG document, and duplicate it here.
            */
            GetNextToken(q,&q,extent,token);
            if (*token == '\0')
              {
                status=MagickFalse;
                break;
              }
            clip_path=GetNodeByURL(primitive,token);
            if (clip_path != (char *) NULL)
              {
                if (graphic_context[n]->clipping_mask != (Image *) NULL)
                  graphic_context[n]->clipping_mask=
                    DestroyImage(graphic_context[n]->clipping_mask);
                graphic_context[n]->clipping_mask=DrawClippingMask(image,
                  graphic_context[n],token,clip_path,&image->exception);
                clip_path=DestroyString(clip_path);
                if (draw_info->compliance != SVGCompliance)
                  (void) DrawClipPath(image,graphic_context[n],
                    graphic_context[n]->clip_mask);
              }
            break;
          }
        if (LocaleCompare("clip-rule",keyword) == 0)
          {
            ssize_t
              fill_rule;

            GetNextToken(q,&q,extent,token);
            fill_rule=ParseCommandOption(MagickFillRuleOptions,MagickFalse,
              token);
            if (fill_rule == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->fill_rule=(FillRule) fill_rule;
            break;
          }
        if (LocaleCompare("clip-units",keyword) == 0)
          {
            ssize_t
              clip_units;

            GetNextToken(q,&q,extent,token);
            clip_units=ParseCommandOption(MagickClipPathOptions,MagickFalse,
              token);
            if (clip_units == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->clip_units=(ClipPathUnits) clip_units;
            if (clip_units == ObjectBoundingBox)
              {
                GetAffineMatrix(&current);
                affine.sx=draw_info->bounds.x2;
                affine.sy=draw_info->bounds.y2;
                affine.tx=draw_info->bounds.x1;
                affine.ty=draw_info->bounds.y1;
                break;
              }
            break;
          }
        if (LocaleCompare("circle",keyword) == 0)
          {
            primitive_type=CirclePrimitive;
            break;
          }
        if (LocaleCompare("color",keyword) == 0)
          {
            primitive_type=ColorPrimitive;
            break;
          }
        if (LocaleCompare("compliance",keyword) == 0)
          {
            /*
              MVG compliance associates a clipping mask with an image; SVG
              compliance associates a clipping mask with a graphics context.
            */
            GetNextToken(q,&q,extent,token);
            graphic_context[n]->compliance=(ComplianceType) ParseCommandOption(
              MagickComplianceOptions,MagickFalse,token);
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'd':
      case 'D':
      {
        if (LocaleCompare("decorate",keyword) == 0)
          {
            ssize_t
              decorate;

            GetNextToken(q,&q,extent,token);
            decorate=ParseCommandOption(MagickDecorateOptions,MagickFalse,
              token);
            if (decorate == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->decorate=(DecorationType) decorate;
            break;
          }
        if (LocaleCompare("density",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            (void) CloneString(&graphic_context[n]->density,token);
            break;
          }
        if (LocaleCompare("direction",keyword) == 0)
          {
            ssize_t
              direction;

            GetNextToken(q,&q,extent,token);
            direction=ParseCommandOption(MagickDirectionOptions,MagickFalse,
              token);
            if (direction == -1)
              status=MagickFalse;
            else
              graphic_context[n]->direction=(DirectionType) direction;
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'e':
      case 'E':
      {
        if (LocaleCompare("ellipse",keyword) == 0)
          {
            primitive_type=EllipsePrimitive;
            break;
          }
        if (LocaleCompare("encoding",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            (void) CloneString(&graphic_context[n]->encoding,token);
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'f':
      case 'F':
      {
        if (LocaleCompare("fill",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            if (graphic_context[n]->clip_path != MagickFalse)
              break;
            (void) FormatLocaleString(pattern,MaxTextExtent,"%s",token);
            if (GetImageArtifact(image,pattern) != (const char *) NULL)
              (void) DrawPatternPath(image,draw_info,token,
                &graphic_context[n]->fill_pattern);
            else
              {
                status&=QueryColorDatabase(token,&graphic_context[n]->fill,
                  &image->exception);
                if (graphic_context[n]->fill_opacity != OpaqueOpacity)
                  graphic_context[n]->fill.opacity=(Quantum)
                    graphic_context[n]->fill_opacity;
              }
            break;
          }
        if (LocaleCompare("fill-opacity",keyword) == 0)
          {
            double
              opacity;

            GetNextToken(q,&q,extent,token);
            if (graphic_context[n]->clip_path != MagickFalse)
              break;
            factor=strchr(token,'%') != (char *) NULL ? 0.01 : 1.0;
            opacity=MagickMin(MagickMax(factor*
              StringToDouble(token,&next_token),0.0),1.0);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            graphic_context[n]->fill_opacity=QuantumRange*(1.0-opacity);
            break;
          }
        if (LocaleCompare("fill-rule",keyword) == 0)
          {
            ssize_t
              fill_rule;

            GetNextToken(q,&q,extent,token);
            fill_rule=ParseCommandOption(MagickFillRuleOptions,MagickFalse,
              token);
            if (fill_rule == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->fill_rule=(FillRule) fill_rule;
            break;
          }
        if (LocaleCompare("font",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            (void) CloneString(&graphic_context[n]->font,token);
            if (LocaleCompare("none",token) == 0)
              graphic_context[n]->font=(char *) RelinquishMagickMemory(
                graphic_context[n]->font);
            break;
          }
        if (LocaleCompare("font-family",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            (void) CloneString(&graphic_context[n]->family,token);
            break;
          }
        if (LocaleCompare("font-size",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            graphic_context[n]->pointsize=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        if (LocaleCompare("font-stretch",keyword) == 0)
          {
            ssize_t
              stretch;

            GetNextToken(q,&q,extent,token);
            stretch=ParseCommandOption(MagickStretchOptions,MagickFalse,token);
            if (stretch == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->stretch=(StretchType) stretch;
            break;
          }
        if (LocaleCompare("font-style",keyword) == 0)
          {
            ssize_t
              style;

            GetNextToken(q,&q,extent,token);
            style=ParseCommandOption(MagickStyleOptions,MagickFalse,token);
            if (style == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->style=(StyleType) style;
            break;
          }
        if (LocaleCompare("font-weight",keyword) == 0)
          {
            ssize_t
              weight;

            GetNextToken(q,&q,extent,token);
            weight=ParseCommandOption(MagickWeightOptions,MagickFalse,token);
            if (weight == -1)
              weight=(ssize_t) StringToUnsignedLong(token);
            graphic_context[n]->weight=(size_t) weight;
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'g':
      case 'G':
      {
        if (LocaleCompare("gradient-units",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            break;
          }
        if (LocaleCompare("gravity",keyword) == 0)
          {
            ssize_t
              gravity;

            GetNextToken(q,&q,extent,token);
            gravity=ParseCommandOption(MagickGravityOptions,MagickFalse,token);
            if (gravity == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->gravity=(GravityType) gravity;
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'i':
      case 'I':
      {
        if (LocaleCompare("image",keyword) == 0)
          {
            ssize_t
              compose;

            primitive_type=ImagePrimitive;
            GetNextToken(q,&q,extent,token);
            compose=ParseCommandOption(MagickComposeOptions,MagickFalse,token);
            if (compose == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->compose=(CompositeOperator) compose;
            break;
          }
        if (LocaleCompare("interline-spacing",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            graphic_context[n]->interline_spacing=StringToDouble(token,
              &next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        if (LocaleCompare("interword-spacing",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            graphic_context[n]->interword_spacing=StringToDouble(token,
              &next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'k':
      case 'K':
      {
        if (LocaleCompare("kerning",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            graphic_context[n]->kerning=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'l':
      case 'L':
      {
        if (LocaleCompare("line",keyword) == 0)
          {
            primitive_type=LinePrimitive;
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'm':
      case 'M':
      {
        if (LocaleCompare("mask",keyword) == 0)
          {
            char
              *mask_path;

            /*
              Take a node from within the MVG document, and duplicate it here.
            */
            GetNextToken(q,&q,extent,token);
            mask_path=GetNodeByURL(primitive,token);
            if (mask_path != (char *) NULL)
              {
                if (graphic_context[n]->composite_mask != (Image *) NULL)
                  graphic_context[n]->composite_mask=
                    DestroyImage(graphic_context[n]->composite_mask);
                graphic_context[n]->composite_mask=DrawCompositeMask(image,
                  graphic_context[n],token,mask_path,&image->exception);
                mask_path=DestroyString(mask_path);
                if (draw_info->compliance != SVGCompliance)
                  status=SetImageMask(image,graphic_context[n]->composite_mask);
              }
            break;
          }
        if (LocaleCompare("matte",keyword) == 0)
          {
            primitive_type=MattePrimitive;
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'o':
      case 'O':
      {
        if (LocaleCompare("offset",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            break;
          }
        if (LocaleCompare("opacity",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            if (graphic_context[n]->clip_path != MagickFalse)
              break;
            factor=strchr(token,'%') != (char *) NULL ? 0.01 : 1.0;
            graphic_context[n]->opacity=(Quantum) (QuantumRange-QuantumRange*
              ((1.0-QuantumScale*graphic_context[n]->opacity)*factor*
              StringToDouble(token,&next_token)));
            graphic_context[n]->fill_opacity=QuantumRange-QuantumRange*((1.0-
              QuantumScale*graphic_context[n]->fill_opacity)*factor*
              StringToDouble(token,&next_token));
            graphic_context[n]->stroke_opacity=QuantumRange-QuantumRange*((1.0-
              QuantumScale*graphic_context[n]->stroke_opacity)*factor*
              StringToDouble(token,&next_token));
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'p':
      case 'P':
      {
        if (LocaleCompare("path",keyword) == 0)
          {
            primitive_type=PathPrimitive;
            break;
          }
        if (LocaleCompare("point",keyword) == 0)
          {
            primitive_type=PointPrimitive;
            break;
          }
        if (LocaleCompare("polyline",keyword) == 0)
          {
            primitive_type=PolylinePrimitive;
            break;
          }
        if (LocaleCompare("polygon",keyword) == 0)
          {
            primitive_type=PolygonPrimitive;
            break;
          }
        if (LocaleCompare("pop",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            if (LocaleCompare("clip-path",token) == 0)
              break;
            if (LocaleCompare("defs",token) == 0)
              {
                defsDepth--;
                graphic_context[n]->render=defsDepth > 0 ? MagickFalse :
                  MagickTrue;
                break;
              }
            if (LocaleCompare("gradient",token) == 0)
              break;
            if (LocaleCompare("graphic-context",token) == 0)
              {
                if (n <= 0)
                  {
                    (void) ThrowMagickException(&image->exception,
                      GetMagickModule(),DrawError,
                      "UnbalancedGraphicContextPushPop","`%s'",token);
                    status=MagickFalse;
                    n=0;
                    break;
                  }
                if ((graphic_context[n]->clip_mask != (char *) NULL) &&
                    (draw_info->compliance != SVGCompliance))
                  if (LocaleCompare(graphic_context[n]->clip_mask,
                      graphic_context[n-1]->clip_mask) != 0)
                    (void) SetImageClipMask(image,(Image *) NULL);
                graphic_context[n]=DestroyDrawInfo(graphic_context[n]);
                n--;
                break;
              }
            if (LocaleCompare("mask",token) == 0)
              break;
            if (LocaleCompare("pattern",token) == 0)
              break;
            if (LocaleCompare("symbol",token) == 0)
              {
                symbolDepth--;
                graphic_context[n]->render=symbolDepth > 0 ? MagickFalse :
                  MagickTrue;
                break;
              }
            status=MagickFalse;
            break;
          }
        if (LocaleCompare("push",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            if (LocaleCompare("clip-path",token) == 0)
              {
                char
                  *clip_path,
                  name[MaxTextExtent];

                GetNextToken(q,&q,extent,token);
                (void) FormatLocaleString(name,MaxTextExtent,"%s",token);
                clip_path=GetNodeByURL(primitive,name);
                if (clip_path != (char *) NULL)
                  {
                    (void) SetImageArtifact(image,name,clip_path);
                    clip_path=DestroyString(clip_path);
                  }
                break;
              }
            if (LocaleCompare("defs",token) == 0)
              {
                defsDepth++;
                graphic_context[n]->render=defsDepth > 0 ? MagickFalse :
                  MagickTrue;
                break;
              }
            if (LocaleCompare("gradient",token) == 0)
              {
                char
                  key[2*MaxTextExtent],
                  name[MaxTextExtent],
                  type[MaxTextExtent];

                SegmentInfo
                  segment;

                GetNextToken(q,&q,extent,token);
                (void) CopyMagickString(name,token,MaxTextExtent);
                GetNextToken(q,&q,extent,token);
                (void) CopyMagickString(type,token,MaxTextExtent);
                GetNextToken(q,&q,extent,token);
                segment.x1=StringToDouble(token,&next_token);
                if (token == next_token)
                  ThrowPointExpectedException(image,token);
                GetNextToken(q,&q,extent,token);
                if (*token == ',')
                  GetNextToken(q,&q,extent,token);
                segment.y1=StringToDouble(token,&next_token);
                if (token == next_token)
                  ThrowPointExpectedException(image,token);
                GetNextToken(q,&q,extent,token);
                if (*token == ',')
                  GetNextToken(q,&q,extent,token);
                segment.x2=StringToDouble(token,&next_token);
                if (token == next_token)
                  ThrowPointExpectedException(image,token);
                GetNextToken(q,&q,extent,token);
                if (*token == ',')
                  GetNextToken(q,&q,extent,token);
                segment.y2=StringToDouble(token,&next_token);
                if (token == next_token)
                  ThrowPointExpectedException(image,token);
                if (LocaleCompare(type,"radial") == 0)
                  {
                    GetNextToken(q,&q,extent,token);
                    if (*token == ',')
                      GetNextToken(q,&q,extent,token);
                  }
                for (p=q; *q != '\0'; )
                {
                  GetNextToken(q,&q,extent,token);
                  if (LocaleCompare(token,"pop") != 0)
                    continue;
                  GetNextToken(q,(const char **) NULL,extent,token);
                  if (LocaleCompare(token,"gradient") != 0)
                    continue;
                  break;
                }
                if ((size_t) (q-p-4+1) <= 0)
                  {
                    status=MagickFalse;
                    break;
                  }
                (void) CopyMagickString(token,p,(size_t) (q-p-4+1));
                bounds.x1=graphic_context[n]->affine.sx*segment.x1+
                  graphic_context[n]->affine.ry*segment.y1+
                  graphic_context[n]->affine.tx;
                bounds.y1=graphic_context[n]->affine.rx*segment.x1+
                  graphic_context[n]->affine.sy*segment.y1+
                  graphic_context[n]->affine.ty;
                bounds.x2=graphic_context[n]->affine.sx*segment.x2+
                  graphic_context[n]->affine.ry*segment.y2+
                  graphic_context[n]->affine.tx;
                bounds.y2=graphic_context[n]->affine.rx*segment.x2+
                  graphic_context[n]->affine.sy*segment.y2+
                  graphic_context[n]->affine.ty;
                (void) FormatLocaleString(key,MaxTextExtent,"%s",name);
                (void) SetImageArtifact(image,key,token);
                (void) FormatLocaleString(key,MaxTextExtent,"%s-type",name);
                (void) SetImageArtifact(image,key,type);
                (void) FormatLocaleString(key,MaxTextExtent,"%s-geometry",name);
                (void) FormatLocaleString(geometry,MaxTextExtent,
                  "%gx%g%+.15g%+.15g",
                  MagickMax(fabs(bounds.x2-bounds.x1+1.0),1.0),
                  MagickMax(fabs(bounds.y2-bounds.y1+1.0),1.0),
                  bounds.x1,bounds.y1);
                (void) SetImageArtifact(image,key,geometry);
                GetNextToken(q,&q,extent,token);
                break;
              }
            if (LocaleCompare("graphic-context",token) == 0)
              {
                n++;
                graphic_context=(DrawInfo **) ResizeQuantumMemory(
                  graphic_context,(size_t) (n+1),sizeof(*graphic_context));
                if (graphic_context == (DrawInfo **) NULL)
                  {
                    (void) ThrowMagickException(&image->exception,
                      GetMagickModule(),ResourceLimitError,
                      "MemoryAllocationFailed","`%s'",image->filename);
                    break;
                  }
                graphic_context[n]=CloneDrawInfo((ImageInfo *) NULL,
                  graphic_context[n-1]);
                if (*q == '"')
                  GetNextToken(q,&q,extent,token);
                break;
              }
            if (LocaleCompare("mask",token) == 0)
              {
                GetNextToken(q,&q,extent,token);
                break;
              }
            if (LocaleCompare("pattern",token) == 0)
              {
                RectangleInfo
                  bounds;

                GetNextToken(q,&q,extent,token);
                (void) CopyMagickString(name,token,MaxTextExtent);
                GetNextToken(q,&q,extent,token);
                bounds.x=(ssize_t) ceil(StringToDouble(token,&next_token)-0.5);
                if (token == next_token)
                  ThrowPointExpectedException(image,token);
                GetNextToken(q,&q,extent,token);
                if (*token == ',')
                  GetNextToken(q,&q,extent,token);
                bounds.y=(ssize_t) ceil(StringToDouble(token,&next_token)-0.5);
                if (token == next_token)
                  ThrowPointExpectedException(image,token);
                GetNextToken(q,&q,extent,token);
                if (*token == ',')
                  GetNextToken(q,&q,extent,token);
                bounds.width=(size_t) floor(StringToDouble(token,&next_token)+
                  0.5);
                if (token == next_token)
                  ThrowPointExpectedException(image,token);
                GetNextToken(q,&q,extent,token);
                if (*token == ',')
                  GetNextToken(q,&q,extent,token);
                bounds.height=(size_t) floor(StringToDouble(token,&next_token)+
                  0.5);
                if (token == next_token)
                  ThrowPointExpectedException(image,token);
                for (p=q; *q != '\0'; )
                {
                  GetNextToken(q,&q,extent,token);
                  if (LocaleCompare(token,"pop") != 0)
                    continue;
                  GetNextToken(q,(const char **) NULL,extent,token);
                  if (LocaleCompare(token,"pattern") != 0)
                    continue;
                  break;
                }
                if ((size_t) (q-p-4+1) <= 0)
                  {
                    status=MagickFalse;
                    break;
                  }
                (void) CopyMagickString(token,p,(size_t) (q-p-4+1));
                (void) FormatLocaleString(key,MaxTextExtent,"%s",name);
                (void) SetImageArtifact(image,key,token);
                (void) FormatLocaleString(key,MaxTextExtent,"%s-geometry",name);
                (void) FormatLocaleString(geometry,MaxTextExtent,
                  "%.20gx%.20g%+.20g%+.20g",(double) bounds.width,(double)
                  bounds.height,(double) bounds.x,(double) bounds.y);
                (void) SetImageArtifact(image,key,geometry);
                GetNextToken(q,&q,extent,token);
                break;
              }
            if (LocaleCompare("symbol",token) == 0)
              {
                symbolDepth++;
                graphic_context[n]->render=symbolDepth > 0 ? MagickFalse :
                  MagickTrue;
                break;
              }
            status=MagickFalse;
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'r':
      case 'R':
      {
        if (LocaleCompare("rectangle",keyword) == 0)
          {
            primitive_type=RectanglePrimitive;
            break;
          }
        if (LocaleCompare("rotate",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            angle=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            affine.sx=cos(DegreesToRadians(fmod((double) angle,360.0)));
            affine.rx=sin(DegreesToRadians(fmod((double) angle,360.0)));
            affine.ry=(-sin(DegreesToRadians(fmod((double) angle,360.0))));
            affine.sy=cos(DegreesToRadians(fmod((double) angle,360.0)));
            break;
          }
        if (LocaleCompare("roundRectangle",keyword) == 0)
          {
            primitive_type=RoundRectanglePrimitive;
            break;
          }
        status=MagickFalse;
        break;
      }
      case 's':
      case 'S':
      {
        if (LocaleCompare("scale",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            affine.sx=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(q,&q,extent,token);
            if (*token == ',')
              GetNextToken(q,&q,extent,token);
            affine.sy=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        if (LocaleCompare("skewX",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            angle=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            affine.ry=sin(DegreesToRadians(angle));
            break;
          }
        if (LocaleCompare("skewY",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            angle=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            affine.rx=(-tan(DegreesToRadians(angle)/2.0));
            break;
          }
        if (LocaleCompare("stop-color",keyword) == 0)
          {
            GradientType
              type;

            PixelPacket
              stop_color;

            GetNextToken(q,&q,extent,token);
            status&=QueryColorDatabase(token,&stop_color,&image->exception);
            type=LinearGradient;
            if (draw_info->gradient.type == RadialGradient)
              type=RadialGradient;
            (void) GradientImage(image,type,PadSpread,&start_color,&stop_color);
            start_color=stop_color;
            GetNextToken(q,&q,extent,token);
            break;
          }
        if (LocaleCompare("stroke",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            if (graphic_context[n]->clip_path != MagickFalse)
              break;
            (void) FormatLocaleString(pattern,MaxTextExtent,"%s",token);
            if (GetImageArtifact(image,pattern) != (const char *) NULL)
              (void) DrawPatternPath(image,draw_info,token,
                &graphic_context[n]->stroke_pattern);
            else
              {
                status&=QueryColorDatabase(token,&graphic_context[n]->stroke,
                  &image->exception);
                if (graphic_context[n]->stroke_opacity != OpaqueOpacity)
                  graphic_context[n]->stroke.opacity=(Quantum)
                    graphic_context[n]->stroke_opacity;
              }
            break;
          }
        if (LocaleCompare("stroke-antialias",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            graphic_context[n]->stroke_antialias=
              StringToLong(token) != 0 ? MagickTrue : MagickFalse;
            break;
          }
        if (LocaleCompare("stroke-dasharray",keyword) == 0)
          {
            if (graphic_context[n]->dash_pattern != (double *) NULL)
              graphic_context[n]->dash_pattern=(double *)
                RelinquishMagickMemory(graphic_context[n]->dash_pattern);
            if (IsPoint(q) != MagickFalse)
              {
                const char
                  *p;

                p=q;
                GetNextToken(p,&p,extent,token);
                if (*token == ',')
                  GetNextToken(p,&p,extent,token);
                for (x=0; IsPoint(token) != MagickFalse; x++)
                {
                  GetNextToken(p,&p,extent,token);
                  if (*token == ',')
                    GetNextToken(p,&p,extent,token);
                }
                graphic_context[n]->dash_pattern=(double *)
                  AcquireQuantumMemory((size_t) (2UL*x+2UL),
                  sizeof(*graphic_context[n]->dash_pattern));
                if (graphic_context[n]->dash_pattern == (double *) NULL)
                  {
                    (void) ThrowMagickException(&image->exception,
                      GetMagickModule(),ResourceLimitError,
                      "MemoryAllocationFailed","`%s'",image->filename);
                    status=MagickFalse;
                    break;
                  }
                (void) memset(graphic_context[n]->dash_pattern,0,(size_t)
                  (2UL*x+2UL)*sizeof(*graphic_context[n]->dash_pattern));
                for (j=0; j < x; j++)
                {
                  GetNextToken(q,&q,extent,token);
                  if (*token == ',')
                    GetNextToken(q,&q,extent,token);
                  graphic_context[n]->dash_pattern[j]=StringToDouble(token,
                    &next_token);
                  if (token == next_token)
                    ThrowPointExpectedException(image,token);
                  if (graphic_context[n]->dash_pattern[j] < 0.0)
                    status=MagickFalse;
                }
                if ((x & 0x01) != 0)
                  for ( ; j < (2*x); j++)
                    graphic_context[n]->dash_pattern[j]=
                      graphic_context[n]->dash_pattern[j-x];
                graphic_context[n]->dash_pattern[j]=0.0;
                break;
              }
            GetNextToken(q,&q,extent,token);
            break;
          }
        if (LocaleCompare("stroke-dashoffset",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            graphic_context[n]->dash_offset=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        if (LocaleCompare("stroke-linecap",keyword) == 0)
          {
            ssize_t
              linecap;

            GetNextToken(q,&q,extent,token);
            linecap=ParseCommandOption(MagickLineCapOptions,MagickFalse,token);
            if (linecap == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->linecap=(LineCap) linecap;
            break;
          }
        if (LocaleCompare("stroke-linejoin",keyword) == 0)
          {
            ssize_t
              linejoin;

            GetNextToken(q,&q,extent,token);
            linejoin=ParseCommandOption(MagickLineJoinOptions,MagickFalse,
              token);
            if (linejoin == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->linejoin=(LineJoin) linejoin;
            break;
          }
        if (LocaleCompare("stroke-miterlimit",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            graphic_context[n]->miterlimit=StringToUnsignedLong(token);
            break;
          }
        if (LocaleCompare("stroke-opacity",keyword) == 0)
          {
            double
              opacity;

            GetNextToken(q,&q,extent,token);
            if (graphic_context[n]->clip_path != MagickFalse)
              break;
            factor=strchr(token,'%') != (char *) NULL ? 0.01 : 1.0;
            opacity=MagickMin(MagickMax(factor*
              StringToDouble(token,&next_token),0.0),1.0);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            graphic_context[n]->stroke_opacity=QuantumRange*(1.0-opacity);
            break;
          }
        if (LocaleCompare("stroke-width",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            if (graphic_context[n]->clip_path != MagickFalse)
              break;
            graphic_context[n]->stroke_width=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        status=MagickFalse;
        break;
      }
      case 't':
      case 'T':
      {
        if (LocaleCompare("text",keyword) == 0)
          {
            primitive_type=TextPrimitive;
            break;
          }
        if (LocaleCompare("text-align",keyword) == 0)
          {
            ssize_t
              align;

            GetNextToken(q,&q,extent,token);
            align=ParseCommandOption(MagickAlignOptions,MagickFalse,token);
            if (align == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->align=(AlignType) align;
            break;
          }
        if (LocaleCompare("text-anchor",keyword) == 0)
          {
            ssize_t
              align;

            GetNextToken(q,&q,extent,token);
            align=ParseCommandOption(MagickAlignOptions,MagickFalse,token);
            if (align == -1)
              {
                status=MagickFalse;
                break;
              }
            graphic_context[n]->align=(AlignType) align;
            break;
          }
        if (LocaleCompare("text-antialias",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            graphic_context[n]->text_antialias=StringToLong(token) != 0 ?
              MagickTrue : MagickFalse;
            break;
          }
        if (LocaleCompare("text-undercolor",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            status&=QueryColorDatabase(token,&graphic_context[n]->undercolor,
              &image->exception);
            break;
          }
        if (LocaleCompare("translate",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            affine.tx=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(q,&q,extent,token);
            if (*token == ',')
              GetNextToken(q,&q,extent,token);
            affine.ty=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        status=MagickFalse;
        break;
      }
      case 'u':
      case 'U':
      {
        if (LocaleCompare("use",keyword) == 0)
          {
            char
              *node;

            /*
              Get a node from the MVG document, and "use" it here.
            */
            GetNextToken(q,&q,extent,token);
            node=GetNodeByURL(primitive,token);
            if (node != (char *) NULL)
              {
                DrawInfo
                  *clone_info;

                clone_info=CloneDrawInfo((ImageInfo *) NULL,graphic_context[n]);
                (void) CloneString(&clone_info->primitive,node);
                node=DestroyString(node);
                status=DrawImage(image,clone_info);
                clone_info=DestroyDrawInfo(clone_info);
              }
            break;
          }
        break;
      }
      case 'v':
      case 'V':
      {
        if (LocaleCompare("viewbox",keyword) == 0)
          {
            GetNextToken(q,&q,extent,token);
            graphic_context[n]->viewbox.x=(ssize_t) ceil(StringToDouble(token,
              &next_token)-0.5);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(q,&q,extent,token);
            if (*token == ',')
              GetNextToken(q,&q,extent,token);
            graphic_context[n]->viewbox.y=(ssize_t) ceil(StringToDouble(token,
              &next_token)-0.5);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(q,&q,extent,token);
            if (*token == ',')
              GetNextToken(q,&q,extent,token);
            graphic_context[n]->viewbox.width=(size_t) floor(StringToDouble(
              token,&next_token)+0.5);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(q,&q,extent,token);
            if (*token == ',')
              GetNextToken(q,&q,extent,token);
            graphic_context[n]->viewbox.height=(size_t) floor(StringToDouble(
              token,&next_token)+0.5);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            break;
          }
        status=MagickFalse;
        break;
      }
      default:
      {
        status=MagickFalse;
        break;
      }
    }
    if (status == MagickFalse)
      break;
    if ((fabs(affine.sx-1.0) >= DrawEpsilon) ||
        (fabs(affine.rx) >= DrawEpsilon) || (fabs(affine.ry) >= DrawEpsilon) ||
        (fabs(affine.sy-1.0) >= DrawEpsilon) ||
        (fabs(affine.tx) >= DrawEpsilon) || (fabs(affine.ty) >= DrawEpsilon))
      {
        graphic_context[n]->affine.sx=current.sx*affine.sx+current.ry*affine.rx;
        graphic_context[n]->affine.rx=current.rx*affine.sx+current.sy*affine.rx;
        graphic_context[n]->affine.ry=current.sx*affine.ry+current.ry*affine.sy;
        graphic_context[n]->affine.sy=current.rx*affine.ry+current.sy*affine.sy;
        graphic_context[n]->affine.tx=current.sx*affine.tx+current.ry*affine.ty+
          current.tx;
        graphic_context[n]->affine.ty=current.rx*affine.tx+current.sy*affine.ty+
          current.ty;
      }
    if (primitive_type == UndefinedPrimitive)
      {
        if (image->debug != MagickFalse)
          (void) LogMagickEvent(DrawEvent,GetMagickModule(),"  %.*s",(int)
            (q-p),p);
        continue;
      }
    /*
      Parse the primitive attributes.
    */
    i=0;
    j=0;
    primitive_info[0].point.x=0.0;
    primitive_info[0].point.y=0.0;
    primitive_info[0].coordinates=0;
    primitive_info[0].method=FloodfillMethod;
    primitive_info[0].closed_subpath=MagickFalse;
    for (x=0; *q != '\0'; x++)
    {
      /*
        Define points.
      */
      if (IsPoint(q) == MagickFalse)
        break;
      GetNextToken(q,&q,extent,token);
      point.x=StringToDouble(token,&next_token);
      if (token == next_token)
        ThrowPointExpectedException(image,token);
      GetNextToken(q,&q,extent,token);
      if (*token == ',')
        GetNextToken(q,&q,extent,token);
      point.y=StringToDouble(token,&next_token);
      if (token == next_token)
        ThrowPointExpectedException(image,token);
      GetNextToken(q,(const char **) NULL,extent,token);
      if (*token == ',')
        GetNextToken(q,&q,extent,token);
      primitive_info[i].primitive=primitive_type;
      primitive_info[i].point=point;
      primitive_info[i].coordinates=0;
      primitive_info[i].method=FloodfillMethod;
      primitive_info[i].closed_subpath=MagickFalse;
      i++;
      if (i < (ssize_t) number_points)
        continue;
      number_points<<=1;
      if (number_points != (MagickSizeType) ((size_t) number_points))
        ThrowBinaryException(ResourceLimitError,"MemoryAllocationFailed",
          image->filename);
      primitive_info=(PrimitiveInfo *) ResizeQuantumMemory(primitive_info,
        (size_t) number_points+4096,sizeof(*primitive_info));
      if (primitive_info == (PrimitiveInfo *) NULL)
        ThrowBinaryException(ResourceLimitError,"MemoryAllocationFailed",
          image->filename);
    }
    if (status == MagickFalse)
      break;
    primitive_info[j].primitive=primitive_type;
    primitive_info[j].coordinates=(size_t) x;
    primitive_info[j].method=FloodfillMethod;
    primitive_info[j].closed_subpath=MagickFalse;
    primitive_info[j].text=(char *) NULL;
    /*
      Circumscribe primitive within a circle.
    */
    bounds.x1=primitive_info[j].point.x;
    bounds.y1=primitive_info[j].point.y;
    bounds.x2=primitive_info[j].point.x;
    bounds.y2=primitive_info[j].point.y;
    for (k=1; k < (ssize_t) primitive_info[j].coordinates; k++)
    {
      point=primitive_info[j+k].point;
      if (point.x < bounds.x1)
        bounds.x1=point.x;
      if (point.y < bounds.y1)
        bounds.y1=point.y;
      if (point.x > bounds.x2)
        bounds.x2=point.x;
      if (point.y > bounds.y2)
        bounds.y2=point.y;
    }
    /*
      Speculate how many points our primitive might consume.
    */
    coordinates=primitive_info[j].coordinates;
    switch (primitive_type)
    {
      case RectanglePrimitive:
      {
        coordinates*=5;
        break;
      }
      case RoundRectanglePrimitive:
      {
        coordinates=ReckonRoundRectangleCoordinates(primitive_info[j].point,
          primitive_info[j+1].point,primitive_info[j+2].point);
        break;
      }
      case BezierPrimitive:
      {
        if (primitive_info[j].coordinates > 107)
          {
            (void) ThrowMagickException(&image->exception,GetMagickModule(),
              DrawError,"TooManyBezierCoordinates","`%s'",token);
            status=MagickFalse;
            break;
          }
        coordinates=(BezierQuantum*primitive_info[j].coordinates);
        break;
      }
      case PathPrimitive:
      {
        char
          *s,
          *t;

        GetNextToken(q,&q,extent,token);
        coordinates=1;
        t=token;
        for (s=token; *s != '\0'; s=t)
        {
          double
            value;

          value=StringToDouble(s,&t);
          (void) value;
          if (s == t)
            {
              t++;
              continue;
            }
          coordinates++;
        }
        for (s=token; *s != '\0'; s++)
          if (strspn(s,"AaCcQqSsTt") != 0)
            coordinates+=(20*BezierQuantum)+360;
        break;
      }
      case CirclePrimitive:
      {
        double
          alpha,
          beta,
          radius;

        PointInfo
          offset,
          degrees;

        alpha=primitive_info[j+1].point.x-primitive_info[j].point.x;
        beta=primitive_info[j+1].point.y-primitive_info[j].point.y;
        radius=hypot((double) alpha,(double) beta);
        offset.x=(double) radius;
        offset.y=(double) radius;
        degrees.x=0.0;
        degrees.y=360.0;
        coordinates=ReckonEllipseCoordinates(offset,degrees);
        break;
      }
      case ArcPrimitive:
      {
        PointInfo
          center,
          radii;

        if ((primitive_info[j+2].point.x < -360.0) ||
            (primitive_info[j+2].point.x > 360.0) ||
            (primitive_info[j+2].point.y < -360.0) ||
            (primitive_info[j+2].point.y > 360.0))
          ThrowPointExpectedException(image,token);
        center.x=0.5*(primitive_info[j+1].point.x+primitive_info[j].point.x);
        center.y=0.5*(primitive_info[j+1].point.y+primitive_info[j].point.y);
        radii.x=fabs(center.x-primitive_info[j].point.x);
        radii.y=fabs(center.y-primitive_info[j].point.y);
        coordinates=ReckonEllipseCoordinates(radii,primitive_info[j+2].point);
        break;
      }
      case EllipsePrimitive:
      {
        if ((primitive_info[j+2].point.x < -360.0) ||
            (primitive_info[j+2].point.x > 360.0) ||
            (primitive_info[j+2].point.y < -360.0) ||
            (primitive_info[j+2].point.y > 360.0))
          ThrowPointExpectedException(image,token);
        coordinates=ReckonEllipseCoordinates(primitive_info[j+1].point,
          primitive_info[j+2].point);
        break;
      }
      default:
        break;
    }
    if (coordinates > MaxBezierCoordinates)
      {
        (void) ThrowMagickException(&image->exception,GetMagickModule(),
          DrawError,"TooManyBezierCoordinates","`%s'",token);
        status=MagickFalse;
      }
    if (status == MagickFalse)
      break;
    if ((MagickSizeType) (i+coordinates) >= number_points)
      {
        /*
          Resize based on speculative points required by primitive.
        */
        number_points+=coordinates+1;
        primitive_info=(PrimitiveInfo *) ResizeQuantumMemory(primitive_info,
          (size_t) number_points+4096,sizeof(*primitive_info));
        if ((primitive_info == (PrimitiveInfo *) NULL) ||
            (number_points != (MagickSizeType) ((size_t) number_points)))
          {
            (void) ThrowMagickException(&image->exception,GetMagickModule(),
              ResourceLimitError,"MemoryAllocationFailed","`%s'",
              image->filename);
            break;
          }
      }
    switch (primitive_type)
    {
      case PointPrimitive:
      default:
      {
        if (primitive_info[j].coordinates != 1)
          {
            status=MagickFalse;
            break;
          }
        TracePoint(primitive_info+j,primitive_info[j].point);
        i=(ssize_t) (j+primitive_info[j].coordinates);
        break;
      }
      case LinePrimitive:
      {
        if (primitive_info[j].coordinates != 2)
          {
            status=MagickFalse;
            break;
          }
        TraceLine(primitive_info+j,primitive_info[j].point,
          primitive_info[j+1].point);
        i=(ssize_t) (j+primitive_info[j].coordinates);
        break;
      }
      case RectanglePrimitive:
      {
        if (primitive_info[j].coordinates != 2)
          {
            status=MagickFalse;
            break;
          }
        TraceRectangle(primitive_info+j,primitive_info[j].point,
          primitive_info[j+1].point);
        i=(ssize_t) (j+primitive_info[j].coordinates);
        break;
      }
      case RoundRectanglePrimitive:
      {
        if (primitive_info[j].coordinates != 3)
          {
            status=MagickFalse;
            break;
          }
        if ((primitive_info[j+2].point.x < 0.0) ||
            (primitive_info[j+2].point.y < 0.0))
          {
            status=MagickFalse;
            break;
          }
        if ((primitive_info[j+1].point.x-primitive_info[j].point.x) < 0.0)
          {
            status=MagickFalse;
            break;
          }
        if ((primitive_info[j+1].point.y-primitive_info[j].point.y) < 0.0)
          {
            status=MagickFalse;
            break;
          }
        TraceRoundRectangle(primitive_info+j,primitive_info[j].point,
          primitive_info[j+1].point,primitive_info[j+2].point);
        i=(ssize_t) (j+primitive_info[j].coordinates);
        break;
      }
      case ArcPrimitive:
      {
        if (primitive_info[j].coordinates != 3)
          {
            primitive_type=UndefinedPrimitive;
            break;
          }
        TraceArc(primitive_info+j,primitive_info[j].point,
          primitive_info[j+1].point,primitive_info[j+2].point);
        i=(ssize_t) (j+primitive_info[j].coordinates);
        break;
      }
      case EllipsePrimitive:
      {
        if (primitive_info[j].coordinates != 3)
          {
            status=MagickFalse;
            break;
          }
        if ((primitive_info[j+1].point.x < 0.0) ||
            (primitive_info[j+1].point.y < 0.0))
          {
            status=MagickFalse;
            break;
          }
        TraceEllipse(primitive_info+j,primitive_info[j].point,
          primitive_info[j+1].point,primitive_info[j+2].point);
        i=(ssize_t) (j+primitive_info[j].coordinates);
        break;
      }
      case CirclePrimitive:
      {
        if (primitive_info[j].coordinates != 2)
          {
            status=MagickFalse;
            break;
          }
        TraceCircle(primitive_info+j,primitive_info[j].point,
          primitive_info[j+1].point);
        i=(ssize_t) (j+primitive_info[j].coordinates);
        break;
      }
      case PolylinePrimitive:
      {
        if (primitive_info[j].coordinates < 1)
          {
            status=MagickFalse;
            break;
          }
        break;
      }
      case PolygonPrimitive:
      {
        if (primitive_info[j].coordinates < 3)
          {
            status=MagickFalse;
            break;
          }
        primitive_info[i]=primitive_info[j];
        primitive_info[i].coordinates=0;
        primitive_info[j].coordinates++;
        primitive_info[j].closed_subpath=MagickTrue;
        i++;
        break;
      }
      case BezierPrimitive:
      {
        if (primitive_info[j].coordinates < 3)
          {
            status=MagickFalse;
            break;
          }
        TraceBezier(primitive_info+j,primitive_info[j].coordinates);
        i=(ssize_t) (j+primitive_info[j].coordinates);
        break;
      }
      case PathPrimitive:
      {
        coordinates=TracePath(image,primitive_info+j,token);
        if (coordinates == 0)
          {
            status=MagickFalse;
            break;
          }
        i=(ssize_t) (j+coordinates);
        break;
      }
      case ColorPrimitive:
      case MattePrimitive:
      {
        ssize_t
          method;

        if (primitive_info[j].coordinates != 1)
          {
            status=MagickFalse;
            break;
          }
        GetNextToken(q,&q,extent,token);
        method=ParseCommandOption(MagickMethodOptions,MagickFalse,token);
        if (method == -1)
          {
            status=MagickFalse;
            break;
          }
        primitive_info[j].method=(PaintMethod) method;
        break;
      }
      case TextPrimitive:
      {
        if (primitive_info[j].coordinates != 1)
          {
            status=MagickFalse;
            break;
          }
        if (*token != ',')
          GetNextToken(q,&q,extent,token);
        primitive_info[j].text=AcquireString(token);
        break;
      }
      case ImagePrimitive:
      {
        if (primitive_info[j].coordinates != 2)
          {
            status=MagickFalse;
            break;
          }
        GetNextToken(q,&q,extent,token);
        primitive_info[j].text=AcquireString(token);
        break;
      }
    }
    if (primitive_info == (PrimitiveInfo *) NULL)
      break;
    if (image->debug != MagickFalse)
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),"  %.*s",(int) (q-p),p);
    if (status == MagickFalse)
      break;
    primitive_info[i].primitive=UndefinedPrimitive;
    if (i == 0)
      continue;
    /*
      Transform points.
    */
    for (i=0; primitive_info[i].primitive != UndefinedPrimitive; i++)
    {
      point=primitive_info[i].point;
      primitive_info[i].point.x=graphic_context[n]->affine.sx*point.x+
        graphic_context[n]->affine.ry*point.y+graphic_context[n]->affine.tx;
      primitive_info[i].point.y=graphic_context[n]->affine.rx*point.x+
        graphic_context[n]->affine.sy*point.y+graphic_context[n]->affine.ty;
      point=primitive_info[i].point;
      if (point.x < graphic_context[n]->bounds.x1)
        graphic_context[n]->bounds.x1=point.x;
      if (point.y < graphic_context[n]->bounds.y1)
        graphic_context[n]->bounds.y1=point.y;
      if (point.x > graphic_context[n]->bounds.x2)
        graphic_context[n]->bounds.x2=point.x;
      if (point.y > graphic_context[n]->bounds.y2)
        graphic_context[n]->bounds.y2=point.y;
      if (primitive_info[i].primitive == ImagePrimitive)
        break;
      if (i >= (ssize_t) number_points)
        ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
    }
    if (graphic_context[n]->render != MagickFalse)
      {
        if ((n != 0) && (draw_info->compliance != SVGCompliance) &&
            (graphic_context[n]->clip_mask != (char *) NULL) &&
            (LocaleCompare(graphic_context[n]->clip_mask,
             graphic_context[n-1]->clip_mask) != 0))
          status&=DrawClipPath(image,graphic_context[n],
            graphic_context[n]->clip_mask);
        status&=DrawPrimitive(image,graphic_context[n],primitive_info);
      }
    if (primitive_info->text != (char *) NULL)
      primitive_info->text=(char *) RelinquishMagickMemory(
        primitive_info->text);
    proceed=SetImageProgress(image,RenderImageTag,q-primitive,(MagickSizeType)
      primitive_extent);
    if (proceed == MagickFalse)
      break;
    if (status == 0)
      break;
  }
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"end draw-image");
  /*
    Relinquish resources.
  */
  token=DestroyString(token);
  if (primitive_info != (PrimitiveInfo *) NULL)
    primitive_info=(PrimitiveInfo *) RelinquishMagickMemory(primitive_info);
  primitive=DestroyString(primitive);
  for ( ; n >= 0; n--)
    graphic_context[n]=DestroyDrawInfo(graphic_context[n]);
  graphic_context=(DrawInfo **) RelinquishMagickMemory(graphic_context);
  if (status == MagickFalse)
    ThrowBinaryException(DrawError,"NonconformingDrawingPrimitiveDefinition",
      keyword);
  return(status != 0 ? MagickTrue : MagickFalse);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   D r a w P a t t e r n P a t h                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawPatternPath() draws a pattern.
%
%  The format of the DrawPatternPath method is:
%
%      MagickBooleanType DrawPatternPath(Image *image,const DrawInfo *draw_info,
%        const char *name,Image **pattern)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o name: the pattern name.
%
%    o image: the image.
%
*/
MagickExport MagickBooleanType DrawPatternPath(Image *image,
  const DrawInfo *draw_info,const char *name,Image **pattern)
{
  char
    property[MaxTextExtent];

  const char
    *geometry,
    *path,
    *type;

  DrawInfo
    *clone_info;

  ImageInfo
    *image_info;

  MagickBooleanType
    status;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(draw_info != (const DrawInfo *) NULL);
  assert(name != (const char *) NULL);
  (void) FormatLocaleString(property,MaxTextExtent,"%s",name);
  path=GetImageArtifact(image,property);
  if (path == (const char *) NULL)
    return(MagickFalse);
  (void) FormatLocaleString(property,MaxTextExtent,"%s-geometry",name);
  geometry=GetImageArtifact(image,property);
  if (geometry == (const char *) NULL)
    return(MagickFalse);
  if ((*pattern) != (Image *) NULL)
    *pattern=DestroyImage(*pattern);
  image_info=AcquireImageInfo();
  image_info->size=AcquireString(geometry);
  *pattern=AcquireImage(image_info);
  image_info=DestroyImageInfo(image_info);
  (void) QueryColorDatabase("#00000000",&(*pattern)->background_color,
    &image->exception);
  (void) SetImageBackgroundColor(*pattern);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),
      "begin pattern-path %s %s",name,geometry);
  clone_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  clone_info->fill_pattern=NewImageList();
  clone_info->stroke_pattern=NewImageList();
  (void) FormatLocaleString(property,MaxTextExtent,"%s-type",name);
  type=GetImageArtifact(image,property);
  if (type != (const char *) NULL)
    clone_info->gradient.type=(GradientType) ParseCommandOption(
      MagickGradientOptions,MagickFalse,type);
  (void) CloneString(&clone_info->primitive,path);
  status=DrawImage(*pattern,clone_info);
  clone_info=DestroyDrawInfo(clone_info);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"end pattern-path");
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D r a w P o l y g o n P r i m i t i v e                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawPolygonPrimitive() draws a polygon on the image.
%
%  The format of the DrawPolygonPrimitive method is:
%
%      MagickBooleanType DrawPolygonPrimitive(Image *image,
%        const DrawInfo *draw_info,const PrimitiveInfo *primitive_info)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o primitive_info: Specifies a pointer to a PrimitiveInfo structure.
%
*/

static PolygonInfo **DestroyPolygonThreadSet(PolygonInfo **polygon_info)
{
  register ssize_t
    i;

  assert(polygon_info != (PolygonInfo **) NULL);
  for (i=0; i < (ssize_t) GetMagickResourceLimit(ThreadResource); i++)
    if (polygon_info[i] != (PolygonInfo *) NULL)
      polygon_info[i]=DestroyPolygonInfo(polygon_info[i]);
  polygon_info=(PolygonInfo **) RelinquishMagickMemory(polygon_info);
  return(polygon_info);
}

static PolygonInfo **AcquirePolygonThreadSet(const DrawInfo *draw_info,
  const PrimitiveInfo *primitive_info)
{
  PathInfo
    *magick_restrict path_info;

  PolygonInfo
    **polygon_info;

  register ssize_t
    i;

  size_t
    number_threads;

  number_threads=(size_t) GetMagickResourceLimit(ThreadResource);
  polygon_info=(PolygonInfo **) AcquireQuantumMemory(number_threads,
    sizeof(*polygon_info));
  if (polygon_info == (PolygonInfo **) NULL)
    return((PolygonInfo **) NULL);
  (void) memset(polygon_info,0,(size_t)
    GetMagickResourceLimit(ThreadResource)*sizeof(*polygon_info));
  path_info=ConvertPrimitiveToPath(draw_info,primitive_info);
  if (path_info == (PathInfo *) NULL)
    return(DestroyPolygonThreadSet(polygon_info));
  for (i=0; i < (ssize_t) number_threads; i++)
  {
    polygon_info[i]=ConvertPathToPolygon(path_info);
    if (polygon_info[i] == (PolygonInfo *) NULL)
      return(DestroyPolygonThreadSet(polygon_info));
  }
  path_info=(PathInfo *) RelinquishMagickMemory(path_info);
  return(polygon_info);
}

static double GetOpacityPixel(PolygonInfo *polygon_info,const double mid,
  const MagickBooleanType fill,const FillRule fill_rule,const ssize_t x,
  const ssize_t y,double *stroke_opacity)
{
  double
    alpha,
    beta,
    distance,
    subpath_opacity;

  PointInfo
    delta;

  register EdgeInfo
    *p;

  register const PointInfo
    *q;

  register ssize_t
    i;

  ssize_t
    j,
    winding_number;

  /*
    Compute fill & stroke opacity for this (x,y) point.
  */
  *stroke_opacity=0.0;
  subpath_opacity=0.0;
  p=polygon_info->edges;
  for (j=0; j < (ssize_t) polygon_info->number_edges; j++, p++)
  {
    if ((double) y <= (p->bounds.y1-mid-0.5))
      break;
    if ((double) y > (p->bounds.y2+mid+0.5))
      {
        (void) DestroyEdge(polygon_info,(size_t) j);
        continue;
      }
    if (((double) x <= (p->bounds.x1-mid-0.5)) ||
        ((double) x > (p->bounds.x2+mid+0.5)))
      continue;
    i=(ssize_t) MagickMax((double) p->highwater,1.0);
    for ( ; i < (ssize_t) p->number_points; i++)
    {
      if ((double) y <= (p->points[i-1].y-mid-0.5))
        break;
      if ((double) y > (p->points[i].y+mid+0.5))
        continue;
      if (p->scanline != (double) y)
        {
          p->scanline=(double) y;
          p->highwater=(size_t) i;
        }
      /*
        Compute distance between a point and an edge.
      */
      q=p->points+i-1;
      delta.x=(q+1)->x-q->x;
      delta.y=(q+1)->y-q->y;
      beta=delta.x*(x-q->x)+delta.y*(y-q->y);
      if (beta <= 0.0)
        {
          delta.x=(double) x-q->x;
          delta.y=(double) y-q->y;
          distance=delta.x*delta.x+delta.y*delta.y;
        }
      else
        {
          alpha=delta.x*delta.x+delta.y*delta.y;
          if (beta >= alpha)
            {
              delta.x=(double) x-(q+1)->x;
              delta.y=(double) y-(q+1)->y;
              distance=delta.x*delta.x+delta.y*delta.y;
            }
          else
            {
              alpha=PerceptibleReciprocal(alpha);
              beta=delta.x*(y-q->y)-delta.y*(x-q->x);
              distance=alpha*beta*beta;
            }
        }
      /*
        Compute stroke & subpath opacity.
      */
      beta=0.0;
      if (p->ghostline == MagickFalse)
        {
          alpha=mid+0.5;
          if ((*stroke_opacity < 1.0) &&
              (distance <= ((alpha+0.25)*(alpha+0.25))))
            {
              alpha=mid-0.5;
              if (distance <= ((alpha+0.25)*(alpha+0.25)))
                *stroke_opacity=1.0;
              else
                {
                  beta=1.0;
                  if (fabs(distance-1.0) >= DrawEpsilon)
                    beta=sqrt((double) distance);
                  alpha=beta-mid-0.5;
                  if (*stroke_opacity < ((alpha-0.25)*(alpha-0.25)))
                    *stroke_opacity=(alpha-0.25)*(alpha-0.25);
                }
            }
        }
      if ((fill == MagickFalse) || (distance > 1.0) || (subpath_opacity >= 1.0))
        continue;
      if (distance <= 0.0)
        {
          subpath_opacity=1.0;
          continue;
        }
      if (distance > 1.0)
        continue;
      if (fabs(beta) < DrawEpsilon)
        {
          beta=1.0;
          if (fabs(distance-1.0) >= DrawEpsilon)
            beta=sqrt(distance);
        }
      alpha=beta-1.0;
      if (subpath_opacity < (alpha*alpha))
        subpath_opacity=alpha*alpha;
    }
  }
  /*
    Compute fill opacity.
  */
  if (fill == MagickFalse)
    return(0.0);
  if (subpath_opacity >= 1.0)
    return(1.0);
  /*
    Determine winding number.
  */
  winding_number=0;
  p=polygon_info->edges;
  for (j=0; j < (ssize_t) polygon_info->number_edges; j++, p++)
  {
    if ((double) y <= p->bounds.y1)
      break;
    if (((double) y > p->bounds.y2) || ((double) x <= p->bounds.x1))
      continue;
    if ((double) x > p->bounds.x2)
      {
        winding_number+=p->direction ? 1 : -1;
        continue;
      }
    i=(ssize_t) MagickMax((double) p->highwater,1.0);
    for ( ; i < (ssize_t) (p->number_points-1); i++)
      if ((double) y <= p->points[i].y)
        break;
    q=p->points+i-1;
    if ((((q+1)->x-q->x)*(y-q->y)) <= (((q+1)->y-q->y)*(x-q->x)))
      winding_number+=p->direction ? 1 : -1;
  }
  if (fill_rule != NonZeroRule)
    {
      if ((MagickAbsoluteValue(winding_number) & 0x01) != 0)
        return(1.0);
    }
  else
    if (MagickAbsoluteValue(winding_number) != 0)
      return(1.0);
  return(subpath_opacity);
}

static MagickBooleanType DrawPolygonPrimitive(Image *image,
  const DrawInfo *draw_info,const PrimitiveInfo *primitive_info)
{
  CacheView
    *image_view;

  double
    mid;

  ExceptionInfo
    *exception;

  MagickBooleanType
    fill,
    status;

  PolygonInfo
    **magick_restrict polygon_info;

  register EdgeInfo
    *p;

  register ssize_t
    i;

  SegmentInfo
    bounds;

  ssize_t
    start_y,
    stop_y,
    y;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(draw_info != (DrawInfo *) NULL);
  assert(draw_info->signature == MagickCoreSignature);
  assert(primitive_info != (PrimitiveInfo *) NULL);
  if (primitive_info->coordinates <= 1)
    return(MagickTrue);
  /*
    Compute bounding box.
  */
  polygon_info=AcquirePolygonThreadSet(draw_info,primitive_info);
  if (polygon_info == (PolygonInfo **) NULL)
    return(MagickFalse);
DisableMSCWarning(4127)
  if (0)
    DrawBoundingRectangles(image,draw_info,polygon_info[0]);
RestoreMSCWarning
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"    begin draw-polygon");
  fill=(primitive_info->method == FillToBorderMethod) ||
    (primitive_info->method == FloodfillMethod) ? MagickTrue : MagickFalse;
  mid=ExpandAffine(&draw_info->affine)*SaneStrokeWidth(image,draw_info)/2.0;
  bounds=polygon_info[0]->edges[0].bounds;
  for (i=1; i < (ssize_t) polygon_info[0]->number_edges; i++)
  {
    p=polygon_info[0]->edges+i;
    if (p->bounds.x1 < bounds.x1)
      bounds.x1=p->bounds.x1;
    if (p->bounds.y1 < bounds.y1)
      bounds.y1=p->bounds.y1;
    if (p->bounds.x2 > bounds.x2)
      bounds.x2=p->bounds.x2;
    if (p->bounds.y2 > bounds.y2)
      bounds.y2=p->bounds.y2;
  }
  bounds.x1-=(mid+1.0);
  bounds.y1-=(mid+1.0);
  bounds.x2+=(mid+1.0);
  bounds.y2+=(mid+1.0);
  if ((bounds.x1 >= (double) image->columns) ||
      (bounds.y1 >= (double) image->rows) ||
      (bounds.x2 <= 0.0) || (bounds.y2 <= 0.0))
    {
      polygon_info=DestroyPolygonThreadSet(polygon_info);
      return(MagickTrue);  /* virtual polygon */
    }
  bounds.x1=bounds.x1 < 0.0 ? 0.0 : bounds.x1 >= (double) image->columns-1.0 ?
    (double) image->columns-1.0 : bounds.x1;
  bounds.y1=bounds.y1 < 0.0 ? 0.0 : bounds.y1 >= (double) image->rows-1.0 ?
    (double) image->rows-1.0 : bounds.y1;
  bounds.x2=bounds.x2 < 0.0 ? 0.0 : bounds.x2 >= (double) image->columns-1.0 ?
    (double) image->columns-1.0 : bounds.x2;
  bounds.y2=bounds.y2 < 0.0 ? 0.0 : bounds.y2 >= (double) image->rows-1.0 ?
    (double) image->rows-1.0 : bounds.y2;
  status=MagickTrue;
  exception=(&image->exception);
  image_view=AcquireAuthenticCacheView(image,exception);
  if ((primitive_info->coordinates == 1) ||
      (polygon_info[0]->number_edges == 0))
    {
      /*
        Draw point.
      */
      start_y=(ssize_t) ceil(bounds.y1-0.5);
      stop_y=(ssize_t) floor(bounds.y2+0.5);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
      #pragma omp parallel for schedule(static) shared(status) \
        magick_number_threads(image,image,stop_y-start_y+1,1)
#endif
      for (y=start_y; y <= stop_y; y++)
      {
        MagickBooleanType
          sync;

        register PixelPacket
          *magick_restrict q;

        register ssize_t
          x;

        ssize_t
          start_x,
          stop_x;

        if (status == MagickFalse)
          continue;
        start_x=(ssize_t) ceil(bounds.x1-0.5);
        stop_x=(ssize_t) floor(bounds.x2+0.5);
        x=start_x;
        q=GetCacheViewAuthenticPixels(image_view,x,y,(size_t) (stop_x-x+1),1,
          exception);
        if (q == (PixelPacket *) NULL)
          {
            status=MagickFalse;
            continue;
          }
        for ( ; x <= stop_x; x++)
        {
          if ((x == (ssize_t) ceil(primitive_info->point.x-0.5)) &&
              (y == (ssize_t) ceil(primitive_info->point.y-0.5)))
            (void) GetFillColor(draw_info,x-start_x,y-start_y,q);
          q++;
        }
        sync=SyncCacheViewAuthenticPixels(image_view,exception);
        if (sync == MagickFalse)
          status=MagickFalse;
      }
      image_view=DestroyCacheView(image_view);
      polygon_info=DestroyPolygonThreadSet(polygon_info);
      if (image->debug != MagickFalse)
        (void) LogMagickEvent(DrawEvent,GetMagickModule(),
          "    end draw-polygon");
      return(status);
    }
  /*
    Draw polygon or line.
  */
  start_y=(ssize_t) ceil(bounds.y1-0.5);
  stop_y=(ssize_t) floor(bounds.y2+0.5);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(status) \
    magick_number_threads(image,image,stop_y-start_y+1,1)
#endif
  for (y=start_y; y <= stop_y; y++)
  {
    const int
      id = GetOpenMPThreadId();

    double
      fill_opacity,
      stroke_opacity;

    PixelPacket
      fill_color,
      stroke_color;

    register PixelPacket
      *magick_restrict q;

    register ssize_t
      x;

    ssize_t
      start_x,
      stop_x;

    if (status == MagickFalse)
      continue;
    start_x=(ssize_t) ceil(bounds.x1-0.5);
    stop_x=(ssize_t) floor(bounds.x2+0.5);
    q=GetCacheViewAuthenticPixels(image_view,start_x,y,(size_t) (stop_x-start_x+
      1),1,exception);
    if (q == (PixelPacket *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    for (x=start_x; x <= stop_x; x++)
    {
      /*
        Fill and/or stroke.
      */
      fill_opacity=GetOpacityPixel(polygon_info[id],mid,fill,
        draw_info->fill_rule,x,y,&stroke_opacity);
      if (draw_info->stroke_antialias == MagickFalse)
        {
          fill_opacity=fill_opacity > 0.25 ? 1.0 : 0.0;
          stroke_opacity=stroke_opacity > 0.25 ? 1.0 : 0.0;
        }
      (void) GetFillColor(draw_info,x-start_x,y-start_y,&fill_color);
      fill_opacity=(double) (QuantumRange-fill_opacity*(QuantumRange-
        fill_color.opacity));
      MagickCompositeOver(&fill_color,(MagickRealType) fill_opacity,q,
        (MagickRealType) q->opacity,q);
      (void) GetStrokeColor(draw_info,x-start_x,y-start_y,&stroke_color);
      stroke_opacity=(double) (QuantumRange-stroke_opacity*(QuantumRange-
        stroke_color.opacity));
      MagickCompositeOver(&stroke_color,(MagickRealType) stroke_opacity,q,
        (MagickRealType) q->opacity,q);
      q++;
    }
    if (SyncCacheViewAuthenticPixels(image_view,exception) == MagickFalse)
      status=MagickFalse;
  }
  image_view=DestroyCacheView(image_view);
  polygon_info=DestroyPolygonThreadSet(polygon_info);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"    end draw-polygon");
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   D r a w P r i m i t i v e                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawPrimitive() draws a primitive (line, rectangle, ellipse) on the image.
%
%  The format of the DrawPrimitive method is:
%
%      MagickBooleanType DrawPrimitive(Image *image,const DrawInfo *draw_info,
%        PrimitiveInfo *primitive_info)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o primitive_info: Specifies a pointer to a PrimitiveInfo structure.
%
*/

static void LogPrimitiveInfo(const PrimitiveInfo *primitive_info)
{
  const char
    *methods[] =
    {
      "point",
      "replace",
      "floodfill",
      "filltoborder",
      "reset",
      "?"
    };

  PointInfo
    p,
    q,
    point;

  register ssize_t
    i,
    x;

  ssize_t
    coordinates,
    y;

  x=(ssize_t) ceil(primitive_info->point.x-0.5);
  y=(ssize_t) ceil(primitive_info->point.y-0.5);
  switch (primitive_info->primitive)
  {
    case PointPrimitive:
    {
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),
        "PointPrimitive %.20g,%.20g %s",(double) x,(double) y,
        methods[primitive_info->method]);
      return;
    }
    case ColorPrimitive:
    {
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),
        "ColorPrimitive %.20g,%.20g %s",(double) x,(double) y,
        methods[primitive_info->method]);
      return;
    }
    case MattePrimitive:
    {
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),
        "MattePrimitive %.20g,%.20g %s",(double) x,(double) y,
        methods[primitive_info->method]);
      return;
    }
    case TextPrimitive:
    {
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),
        "TextPrimitive %.20g,%.20g",(double) x,(double) y);
      return;
    }
    case ImagePrimitive:
    {
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),
        "ImagePrimitive %.20g,%.20g",(double) x,(double) y);
      return;
    }
    default:
      break;
  }
  coordinates=0;
  p=primitive_info[0].point;
  q.x=(-1.0);
  q.y=(-1.0);
  for (i=0; primitive_info[i].primitive != UndefinedPrimitive; i++)
  {
    point=primitive_info[i].point;
    if (coordinates <= 0)
      {
        coordinates=(ssize_t) primitive_info[i].coordinates;
        (void) LogMagickEvent(DrawEvent,GetMagickModule(),
          "    begin open (%.20g)",(double) coordinates);
        p=point;
      }
    point=primitive_info[i].point;
    if ((fabs(q.x-point.x) >= DrawEpsilon) ||
        (fabs(q.y-point.y) >= DrawEpsilon))
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),
        "      %.20g: %.18g,%.18g",(double) coordinates,point.x,point.y);
    else
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),
        "      %.20g: %g %g (duplicate)",(double) coordinates,point.x,point.y);
    q=point;
    coordinates--;
    if (coordinates > 0)
      continue;
    if ((fabs(p.x-point.x) >= DrawEpsilon) ||
        (fabs(p.y-point.y) >= DrawEpsilon))
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),"    end last (%.20g)",
        (double) coordinates);
    else
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),"    end open (%.20g)",
        (double) coordinates);
  }
}

MagickExport MagickBooleanType DrawPrimitive(Image *image,
  const DrawInfo *draw_info,const PrimitiveInfo *primitive_info)
{
  CacheView
    *image_view;

  ExceptionInfo
    *exception;

  MagickStatusType
    status;

  register ssize_t
    i,
    x;

  ssize_t
    y;

  if (image->debug != MagickFalse)
    {
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),
        "  begin draw-primitive");
      (void) LogMagickEvent(DrawEvent,GetMagickModule(),
        "    affine: %g,%g,%g,%g,%g,%g",draw_info->affine.sx,
        draw_info->affine.rx,draw_info->affine.ry,draw_info->affine.sy,
        draw_info->affine.tx,draw_info->affine.ty);
    }
  exception=(&image->exception);
  if ((IsGrayColorspace(image->colorspace) != MagickFalse) &&
      ((IsPixelGray(&draw_info->fill) == MagickFalse) ||
       (IsPixelGray(&draw_info->stroke) == MagickFalse)))
    (void) SetImageColorspace(image,sRGBColorspace);
  status=MagickTrue;
  if (draw_info->compliance == SVGCompliance)
    {
      status=SetImageClipMask(image,draw_info->clipping_mask);
      status&=SetImageMask(image,draw_info->composite_mask);
    }
  x=(ssize_t) ceil(primitive_info->point.x-0.5);
  y=(ssize_t) ceil(primitive_info->point.y-0.5);
  image_view=AcquireAuthenticCacheView(image,exception);
  switch (primitive_info->primitive)
  {
    case PointPrimitive:
    {
      PixelPacket
        fill_color;

      PixelPacket
        *q;

      if ((y < 0) || (y >= (ssize_t) image->rows))
        break;
      if ((x < 0) || (x >= (ssize_t) image->columns))
        break;
      q=GetCacheViewAuthenticPixels(image_view,x,y,1,1,exception);
      if (q == (PixelPacket *) NULL)
        break;
      (void) GetFillColor(draw_info,x,y,&fill_color);
      MagickCompositeOver(&fill_color,(MagickRealType) fill_color.opacity,q,
        (MagickRealType) q->opacity,q);
      status&=SyncCacheViewAuthenticPixels(image_view,exception);
      break;
    }
    case ColorPrimitive:
    {
      switch (primitive_info->method)
      {
        case PointMethod:
        default:
        {
          PixelPacket
            *q;

          q=GetCacheViewAuthenticPixels(image_view,x,y,1,1,exception);
          if (q == (PixelPacket *) NULL)
            break;
          (void) GetFillColor(draw_info,x,y,q);
          status&=SyncCacheViewAuthenticPixels(image_view,exception);
          break;
        }
        case ReplaceMethod:
        {
          MagickBooleanType
            sync;

          PixelPacket
            target;

          status&=GetOneCacheViewVirtualPixel(image_view,x,y,&target,exception);
          for (y=0; y < (ssize_t) image->rows; y++)
          {
            register PixelPacket
              *magick_restrict q;

            q=GetCacheViewAuthenticPixels(image_view,0,y,image->columns,1,
              exception);
            if (q == (PixelPacket *) NULL)
              break;
            for (x=0; x < (ssize_t) image->columns; x++)
            {
              if (IsColorSimilar(image,q,&target) == MagickFalse)
                {
                  q++;
                  continue;
                }
              (void) GetFillColor(draw_info,x,y,q);
              q++;
            }
            sync=SyncCacheViewAuthenticPixels(image_view,exception);
            if (sync == MagickFalse)
              break;
          }
          break;
        }
        case FloodfillMethod:
        case FillToBorderMethod:
        {
          MagickPixelPacket
            target;

          (void) GetOneVirtualMagickPixel(image,x,y,&target,exception);
          if (primitive_info->method == FillToBorderMethod)
            {
              target.red=(MagickRealType) draw_info->border_color.red;
              target.green=(MagickRealType) draw_info->border_color.green;
              target.blue=(MagickRealType) draw_info->border_color.blue;
            }
          status&=FloodfillPaintImage(image,DefaultChannels,draw_info,&target,x,
            y,primitive_info->method == FloodfillMethod ? MagickFalse :
            MagickTrue);
          break;
        }
        case ResetMethod:
        {
          MagickBooleanType
            sync;

          for (y=0; y < (ssize_t) image->rows; y++)
          {
            register PixelPacket
              *magick_restrict q;

            register ssize_t
              x;

            q=GetCacheViewAuthenticPixels(image_view,0,y,image->columns,1,
              exception);
            if (q == (PixelPacket *) NULL)
              break;
            for (x=0; x < (ssize_t) image->columns; x++)
            {
              (void) GetFillColor(draw_info,x,y,q);
              q++;
            }
            sync=SyncCacheViewAuthenticPixels(image_view,exception);
            if (sync == MagickFalse)
              break;
          }
          break;
        }
      }
      break;
    }
    case MattePrimitive:
    {
      if (image->matte == MagickFalse)
        (void) SetImageAlphaChannel(image,OpaqueAlphaChannel);
      switch (primitive_info->method)
      {
        case PointMethod:
        default:
        {
          PixelPacket
            pixel;

          PixelPacket
            *q;

          q=GetCacheViewAuthenticPixels(image_view,x,y,1,1,exception);
          if (q == (PixelPacket *) NULL)
            break;
          (void) GetFillColor(draw_info,x,y,&pixel);
          SetPixelOpacity(q,pixel.opacity);
          status&=SyncCacheViewAuthenticPixels(image_view,exception);
          break;
        }
        case ReplaceMethod:
        {
          MagickBooleanType
            sync;

          PixelPacket
            pixel,
            target;

          status&=GetOneCacheViewVirtualPixel(image_view,x,y,&target,exception);
          for (y=0; y < (ssize_t) image->rows; y++)
          {
            register PixelPacket
              *magick_restrict q;

            register ssize_t
              x;

            q=GetCacheViewAuthenticPixels(image_view,0,y,image->columns,1,
              exception);
            if (q == (PixelPacket *) NULL)
              break;
            for (x=0; x < (ssize_t) image->columns; x++)
            {
              if (IsColorSimilar(image,q,&target) == MagickFalse)
                {
                  q++;
                  continue;
                }
              (void) GetFillColor(draw_info,x,y,&pixel);
              SetPixelOpacity(q,pixel.opacity);
              q++;
            }
            sync=SyncCacheViewAuthenticPixels(image_view,exception);
            if (sync == MagickFalse)
              break;
          }
          break;
        }
        case FloodfillMethod:
        case FillToBorderMethod:
        {
          MagickPixelPacket
            target;

          (void) GetOneVirtualMagickPixel(image,x,y,&target,exception);
          if (primitive_info->method == FillToBorderMethod)
            {
              target.red=(MagickRealType) draw_info->border_color.red;
              target.green=(MagickRealType) draw_info->border_color.green;
              target.blue=(MagickRealType) draw_info->border_color.blue;
            }
          status&=FloodfillPaintImage(image,OpacityChannel,draw_info,&target,x,
            y,primitive_info->method == FloodfillMethod ? MagickFalse :
            MagickTrue);
          break;
        }
        case ResetMethod:
        {
          MagickBooleanType
            sync;

          PixelPacket
            pixel;

          for (y=0; y < (ssize_t) image->rows; y++)
          {
            register PixelPacket
              *magick_restrict q;

            register ssize_t
              x;

            q=GetCacheViewAuthenticPixels(image_view,0,y,image->columns,1,
              exception);
            if (q == (PixelPacket *) NULL)
              break;
            for (x=0; x < (ssize_t) image->columns; x++)
            {
              (void) GetFillColor(draw_info,x,y,&pixel);
              SetPixelOpacity(q,pixel.opacity);
              q++;
            }
            sync=SyncCacheViewAuthenticPixels(image_view,exception);
            if (sync == MagickFalse)
              break;
          }
          break;
        }
      }
      break;
    }
    case TextPrimitive:
    {
      char
        geometry[MaxTextExtent];

      DrawInfo
        *clone_info;

      if (primitive_info->text == (char *) NULL)
        break;
      clone_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
      (void) CloneString(&clone_info->text,primitive_info->text);
      (void) FormatLocaleString(geometry,MaxTextExtent,"%+f%+f",
        primitive_info->point.x,primitive_info->point.y);
      (void) CloneString(&clone_info->geometry,geometry);
      status&=AnnotateImage(image,clone_info);
      clone_info=DestroyDrawInfo(clone_info);
      break;
    }
    case ImagePrimitive:
    {
      AffineMatrix
        affine;

      char
        composite_geometry[MaxTextExtent];

      Image
        *composite_image;

      ImageInfo
        *clone_info;

      RectangleInfo
        geometry;

      ssize_t
        x1,
        y1;

      if (primitive_info->text == (char *) NULL)
        break;
      clone_info=AcquireImageInfo();
      if (LocaleNCompare(primitive_info->text,"data:",5) == 0)
        composite_image=ReadInlineImage(clone_info,primitive_info->text,
          &image->exception);
      else
        {
          (void) CopyMagickString(clone_info->filename,primitive_info->text,
            MaxTextExtent);
          composite_image=ReadImage(clone_info,&image->exception);
        }
      clone_info=DestroyImageInfo(clone_info);
      if (composite_image == (Image *) NULL)
        break;
      (void) SetImageProgressMonitor(composite_image,(MagickProgressMonitor)
        NULL,(void *) NULL);
      x1=(ssize_t) ceil(primitive_info[1].point.x-0.5);
      y1=(ssize_t) ceil(primitive_info[1].point.y-0.5);
      if (((x1 != 0L) && (x1 != (ssize_t) composite_image->columns)) ||
          ((y1 != 0L) && (y1 != (ssize_t) composite_image->rows)))
        {
          char
            geometry[MaxTextExtent];

          /*
            Resize image.
          */
          (void) FormatLocaleString(geometry,MaxTextExtent,"%gx%g!",
            primitive_info[1].point.x,primitive_info[1].point.y);
          composite_image->filter=image->filter;
          (void) TransformImage(&composite_image,(char *) NULL,geometry);
        }
      if (composite_image->matte == MagickFalse)
        (void) SetImageAlphaChannel(composite_image,OpaqueAlphaChannel);
      if (draw_info->opacity != OpaqueOpacity)
        (void) SetImageOpacity(composite_image,draw_info->opacity);
      SetGeometry(image,&geometry);
      image->gravity=draw_info->gravity;
      geometry.x=x;
      geometry.y=y;
      (void) FormatLocaleString(composite_geometry,MaxTextExtent,
        "%.20gx%.20g%+.20g%+.20g",(double) composite_image->columns,(double)
        composite_image->rows,(double) geometry.x,(double) geometry.y);
      (void) ParseGravityGeometry(image,composite_geometry,&geometry,
        &image->exception);
      affine=draw_info->affine;
      affine.tx=(double) geometry.x;
      affine.ty=(double) geometry.y;
      composite_image->interpolate=image->interpolate;
      if ((draw_info->compose == OverCompositeOp) ||
          (draw_info->compose == SrcOverCompositeOp))
        (void) DrawAffineImage(image,composite_image,&affine);
      else
        (void) CompositeImage(image,draw_info->compose,composite_image,
          geometry.x,geometry.y);
      composite_image=DestroyImage(composite_image);
      break;
    }
    default:
    {
      double
        mid,
        scale;

      DrawInfo
        *clone_info;

      if (IsEventLogging() != MagickFalse)
        LogPrimitiveInfo(primitive_info);
      scale=ExpandAffine(&draw_info->affine);
      if ((draw_info->dash_pattern != (double *) NULL) &&
          (fabs(draw_info->dash_pattern[0]) >= DrawEpsilon) &&
          (fabs(scale*draw_info->stroke_width) >= DrawEpsilon) &&
          (draw_info->stroke.opacity != (Quantum) TransparentOpacity))
        {
          /*
            Draw dash polygon.
          */
          clone_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
          clone_info->stroke_width=0.0;
          clone_info->stroke.opacity=(Quantum) TransparentOpacity;
          status&=DrawPolygonPrimitive(image,clone_info,primitive_info);
          clone_info=DestroyDrawInfo(clone_info);
          (void) DrawDashPolygon(draw_info,primitive_info,image);
          break;
        }
      mid=ExpandAffine(&draw_info->affine)*SaneStrokeWidth(image,draw_info)/2.0;
      if ((mid > 1.0) &&
          ((draw_info->stroke.opacity != (Quantum) TransparentOpacity) ||
           (draw_info->stroke_pattern != (Image *) NULL)))
        {
          MagickBooleanType
            closed_path;

          /*
            Draw strokes while respecting line cap/join attributes.
          */
          for (i=0; primitive_info[i].primitive != UndefinedPrimitive; i++) ;
          closed_path=primitive_info[0].closed_subpath;
          i=(ssize_t) primitive_info[0].coordinates;
          if ((((draw_info->linecap == RoundCap) ||
                (closed_path != MagickFalse)) &&
               (draw_info->linejoin == RoundJoin)) ||
               (primitive_info[i].primitive != UndefinedPrimitive))
            {
              (void) DrawPolygonPrimitive(image,draw_info,primitive_info);
              break;
            }
          clone_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
          clone_info->stroke_width=0.0;
          clone_info->stroke.opacity=(Quantum) TransparentOpacity;
          status&=DrawPolygonPrimitive(image,clone_info,primitive_info);
          clone_info=DestroyDrawInfo(clone_info);
          status&=DrawStrokePolygon(image,draw_info,primitive_info);
          break;
        }
      status&=DrawPolygonPrimitive(image,draw_info,primitive_info);
      break;
    }
  }
  image_view=DestroyCacheView(image_view);
  if (draw_info->compliance == SVGCompliance)
    {
      status&=SetImageClipMask(image,(Image *) NULL);
      status&=SetImageMask(image,(Image *) NULL);
    }
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),"  end draw-primitive");
  return(status != 0 ? MagickTrue : MagickFalse);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D r a w S t r o k e P o l y g o n                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DrawStrokePolygon() draws a stroked polygon (line, rectangle, ellipse) on
%  the image while respecting the line cap and join attributes.
%
%  The format of the DrawStrokePolygon method is:
%
%      MagickBooleanType DrawStrokePolygon(Image *image,
%        const DrawInfo *draw_info,const PrimitiveInfo *primitive_info)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o primitive_info: Specifies a pointer to a PrimitiveInfo structure.
%
%
*/

static void DrawRoundLinecap(Image *image,const DrawInfo *draw_info,
  const PrimitiveInfo *primitive_info)
{
  PrimitiveInfo
    linecap[5];

  register ssize_t
    i;

  for (i=0; i < 4; i++)
    linecap[i]=(*primitive_info);
  linecap[0].coordinates=4;
  linecap[1].point.x+=2.0*DrawEpsilon;
  linecap[2].point.x+=2.0*DrawEpsilon;
  linecap[2].point.y+=2.0*DrawEpsilon;
  linecap[3].point.y+=2.0*DrawEpsilon;
  linecap[4].primitive=UndefinedPrimitive;
  (void) DrawPolygonPrimitive(image,draw_info,linecap);
}

static MagickBooleanType DrawStrokePolygon(Image *image,
  const DrawInfo *draw_info,const PrimitiveInfo *primitive_info)
{
  DrawInfo
    *clone_info;

  MagickBooleanType
    closed_path;

  MagickStatusType
    status;

  PrimitiveInfo
    *stroke_polygon;

  register const PrimitiveInfo
    *p,
    *q;

  /*
    Draw stroked polygon.
  */
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),
      "    begin draw-stroke-polygon");
  clone_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  clone_info->fill=draw_info->stroke;
  if (clone_info->fill_pattern != (Image *) NULL)
    clone_info->fill_pattern=DestroyImage(clone_info->fill_pattern);
  if (clone_info->stroke_pattern != (Image *) NULL)
    clone_info->fill_pattern=CloneImage(clone_info->stroke_pattern,0,0,
      MagickTrue,&clone_info->stroke_pattern->exception);
  clone_info->stroke.opacity=(Quantum) TransparentOpacity;
  clone_info->stroke_width=0.0;
  clone_info->fill_rule=NonZeroRule;
  status=MagickTrue;
  for (p=primitive_info; p->primitive != UndefinedPrimitive; p+=p->coordinates)
  {
    if (p->coordinates == 1)
      continue;
    stroke_polygon=TraceStrokePolygon(image,draw_info,p);
    if (stroke_polygon == (PrimitiveInfo *) NULL)
      {
        status=0;
        stroke_polygon=(PrimitiveInfo *) RelinquishMagickMemory(stroke_polygon);
        break;
      }
    status&=DrawPolygonPrimitive(image,clone_info,stroke_polygon);
    stroke_polygon=(PrimitiveInfo *) RelinquishMagickMemory(stroke_polygon);
    if (status == 0)
      break;
    q=p+p->coordinates-1;
    closed_path=p->closed_subpath;
    if ((draw_info->linecap == RoundCap) && (closed_path == MagickFalse))
      {
        DrawRoundLinecap(image,draw_info,p);
        DrawRoundLinecap(image,draw_info,q);
      }
  }
  clone_info=DestroyDrawInfo(clone_info);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(DrawEvent,GetMagickModule(),
      "    end draw-stroke-polygon");
  return(status != 0 ? MagickTrue : MagickFalse);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t A f f i n e M a t r i x                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetAffineMatrix() returns an AffineMatrix initialized to the identity
%  matrix.
%
%  The format of the GetAffineMatrix method is:
%
%      void GetAffineMatrix(AffineMatrix *affine_matrix)
%
%  A description of each parameter follows:
%
%    o affine_matrix: the affine matrix.
%
*/
MagickExport void GetAffineMatrix(AffineMatrix *affine_matrix)
{
  (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  assert(affine_matrix != (AffineMatrix *) NULL);
  (void) memset(affine_matrix,0,sizeof(*affine_matrix));
  affine_matrix->sx=1.0;
  affine_matrix->sy=1.0;
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t D r a w I n f o                                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetDrawInfo() initializes draw_info to default values from image_info.
%
%  The format of the GetDrawInfo method is:
%
%      void GetDrawInfo(const ImageInfo *image_info,DrawInfo *draw_info)
%
%  A description of each parameter follows:
%
%    o image_info: the image info..
%
%    o draw_info: the draw info.
%
*/
MagickExport void GetDrawInfo(const ImageInfo *image_info,DrawInfo *draw_info)
{
  char
    *next_token;

  const char
    *option;

  ExceptionInfo
    *exception;

  ImageInfo
    *clone_info;

  /*
    Initialize draw attributes.
  */
  (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  assert(draw_info != (DrawInfo *) NULL);
  (void) memset(draw_info,0,sizeof(*draw_info));
  clone_info=CloneImageInfo(image_info);
  GetAffineMatrix(&draw_info->affine);
  exception=AcquireExceptionInfo();
  (void) QueryColorDatabase("#000F",&draw_info->fill,exception);
  (void) QueryColorDatabase("#FFF0",&draw_info->stroke,exception);
  draw_info->stroke_antialias=clone_info->antialias;
  draw_info->stroke_width=1.0;
  draw_info->fill_rule=EvenOddRule;
  draw_info->fill_opacity=OpaqueOpacity;
  draw_info->stroke_opacity=OpaqueOpacity;
  draw_info->linecap=ButtCap;
  draw_info->linejoin=MiterJoin;
  draw_info->miterlimit=10;
  draw_info->decorate=NoDecoration;
  if (clone_info->font != (char *) NULL)
    draw_info->font=AcquireString(clone_info->font);
  if (clone_info->density != (char *) NULL)
    draw_info->density=AcquireString(clone_info->density);
  draw_info->text_antialias=clone_info->antialias;
  draw_info->pointsize=12.0;
  if (fabs(clone_info->pointsize) >= DrawEpsilon)
    draw_info->pointsize=clone_info->pointsize;
  draw_info->undercolor.opacity=(Quantum) TransparentOpacity;
  draw_info->border_color=clone_info->border_color;
  draw_info->compose=OverCompositeOp;
  if (clone_info->server_name != (char *) NULL)
    draw_info->server_name=AcquireString(clone_info->server_name);
  draw_info->render=MagickTrue;
  draw_info->clip_path=MagickFalse;
  draw_info->debug=IsEventLogging();
  option=GetImageOption(clone_info,"direction");
  if (option != (const char *) NULL)
    draw_info->direction=(DirectionType) ParseCommandOption(
      MagickDirectionOptions,MagickFalse,option);
  else
    draw_info->direction=UndefinedDirection;
  option=GetImageOption(clone_info,"encoding");
  if (option != (const char *) NULL)
    (void) CloneString(&draw_info->encoding,option);
  option=GetImageOption(clone_info,"family");
  if (option != (const char *) NULL)
    (void) CloneString(&draw_info->family,option);
  option=GetImageOption(clone_info,"fill");
  if (option != (const char *) NULL)
    (void) QueryColorDatabase(option,&draw_info->fill,exception);
  option=GetImageOption(clone_info,"gravity");
  if (option != (const char *) NULL)
    draw_info->gravity=(GravityType) ParseCommandOption(MagickGravityOptions,
      MagickFalse,option);
  option=GetImageOption(clone_info,"interline-spacing");
  if (option != (const char *) NULL)
    draw_info->interline_spacing=StringToDouble(option,&next_token);
  option=GetImageOption(clone_info,"interword-spacing");
  if (option != (const char *) NULL)
    draw_info->interword_spacing=StringToDouble(option,&next_token);
  option=GetImageOption(clone_info,"kerning");
  if (option != (const char *) NULL)
    draw_info->kerning=StringToDouble(option,&next_token);
  option=GetImageOption(clone_info,"stroke");
  if (option != (const char *) NULL)
    (void) QueryColorDatabase(option,&draw_info->stroke,exception);
  option=GetImageOption(clone_info,"strokewidth");
  if (option != (const char *) NULL)
    draw_info->stroke_width=StringToDouble(option,&next_token);
  option=GetImageOption(clone_info,"style");
  if (option != (const char *) NULL)
    draw_info->style=(StyleType) ParseCommandOption(MagickStyleOptions,
      MagickFalse,option);
  option=GetImageOption(clone_info,"undercolor");
  if (option != (const char *) NULL)
    (void) QueryColorDatabase(option,&draw_info->undercolor,exception);
  option=GetImageOption(clone_info,"weight");
  if (option != (const char *) NULL)
    {
      ssize_t
        weight;

      weight=ParseCommandOption(MagickWeightOptions,MagickFalse,option);
      if (weight == -1)
        weight=(ssize_t) StringToUnsignedLong(option);
      draw_info->weight=(size_t) weight;
    }
  exception=DestroyExceptionInfo(exception);
  draw_info->signature=MagickCoreSignature;
  clone_info=DestroyImageInfo(clone_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   P e r m u t a t e                                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  Permutate() returns the permuation of the (n,k).
%
%  The format of the Permutate method is:
%
%      void Permutate(ssize_t n,ssize_t k)
%
%  A description of each parameter follows:
%
%    o n:
%
%    o k:
%
%
*/
static inline double Permutate(const ssize_t n,const ssize_t k)
{
  double
    r;

  register ssize_t
    i;

  r=1.0;
  for (i=k+1; i <= n; i++)
    r*=i;
  for (i=1; i <= (n-k); i++)
    r/=i;
  return(r);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   T r a c e P r i m i t i v e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  TracePrimitive is a collection of methods for generating graphic
%  primitives such as arcs, ellipses, paths, etc.
%
*/

static void TraceArc(PrimitiveInfo *primitive_info,const PointInfo start,
  const PointInfo end,const PointInfo arc)
{
  PointInfo
    center,
    radius;

  center.x=0.5*(end.x+start.x);
  center.y=0.5*(end.y+start.y);
  radius.x=fabs(center.x-start.x);
  radius.y=fabs(center.y-start.y);
  TraceEllipse(primitive_info,center,radius,arc);
}

static void TraceArcPath(PrimitiveInfo *primitive_info,const PointInfo start,
  const PointInfo end,const PointInfo arc,const double angle,
  const MagickBooleanType large_arc,const MagickBooleanType sweep)
{
  double
    alpha,
    beta,
    delta,
    factor,
    gamma,
    theta;

  PointInfo
    center,
    points[3],
    radii;

  register double
    cosine,
    sine;

  register PrimitiveInfo
    *p;

  register ssize_t
    i;

  size_t
    arc_segments;

  if ((fabs(start.x-end.x) < DrawEpsilon) &&
      (fabs(start.y-end.y) < DrawEpsilon))
    {
      TracePoint(primitive_info,end);
      return;
    }
  radii.x=fabs(arc.x);
  radii.y=fabs(arc.y);
  if ((fabs(radii.x) < DrawEpsilon) || (fabs(radii.y) < DrawEpsilon))
    {
      TraceLine(primitive_info,start,end);
      return;
    }
  cosine=cos(DegreesToRadians(fmod((double) angle,360.0)));
  sine=sin(DegreesToRadians(fmod((double) angle,360.0)));
  center.x=(double) (cosine*(end.x-start.x)/2+sine*(end.y-start.y)/2);
  center.y=(double) (cosine*(end.y-start.y)/2-sine*(end.x-start.x)/2);
  delta=(center.x*center.x)/(radii.x*radii.x)+(center.y*center.y)/
    (radii.y*radii.y);
  if (delta < DrawEpsilon)
    {
      TraceLine(primitive_info,start,end);
      return;
    }
  if (delta > 1.0)
    {
      radii.x*=sqrt((double) delta);
      radii.y*=sqrt((double) delta);
    }
  points[0].x=(double) (cosine*start.x/radii.x+sine*start.y/radii.x);
  points[0].y=(double) (cosine*start.y/radii.y-sine*start.x/radii.y);
  points[1].x=(double) (cosine*end.x/radii.x+sine*end.y/radii.x);
  points[1].y=(double) (cosine*end.y/radii.y-sine*end.x/radii.y);
  alpha=points[1].x-points[0].x;
  beta=points[1].y-points[0].y;
  factor=PerceptibleReciprocal(alpha*alpha+beta*beta)-0.25;
  if (factor <= 0.0)
    factor=0.0;
  else
    {
      factor=sqrt((double) factor);
      if (sweep == large_arc)
        factor=(-factor);
    }
  center.x=(double) ((points[0].x+points[1].x)/2-factor*beta);
  center.y=(double) ((points[0].y+points[1].y)/2+factor*alpha);
  alpha=atan2(points[0].y-center.y,points[0].x-center.x);
  theta=atan2(points[1].y-center.y,points[1].x-center.x)-alpha;
  if ((theta < 0.0) && (sweep != MagickFalse))
    theta+=2.0*MagickPI;
  else
    if ((theta > 0.0) && (sweep == MagickFalse))
      theta-=2.0*MagickPI;
  arc_segments=(size_t) ceil(fabs((double) (theta/(0.5*MagickPI+DrawEpsilon))));
  p=primitive_info;
  for (i=0; i < (ssize_t) arc_segments; i++)
  {
    beta=0.5*((alpha+(i+1)*theta/arc_segments)-(alpha+i*theta/arc_segments));
    gamma=(8.0/3.0)*sin(fmod((double) (0.5*beta),DegreesToRadians(360.0)))*
      sin(fmod((double) (0.5*beta),DegreesToRadians(360.0)))/
      sin(fmod((double) beta,DegreesToRadians(360.0)));
    points[0].x=(double) (center.x+cos(fmod((double) (alpha+(double) i*theta/
      arc_segments),DegreesToRadians(360.0)))-gamma*sin(fmod((double) (alpha+
      (double) i*theta/arc_segments),DegreesToRadians(360.0))));
    points[0].y=(double) (center.y+sin(fmod((double) (alpha+(double) i*theta/
      arc_segments),DegreesToRadians(360.0)))+gamma*cos(fmod((double) (alpha+
      (double) i*theta/arc_segments),DegreesToRadians(360.0))));
    points[2].x=(double) (center.x+cos(fmod((double) (alpha+(double) (i+1)*
      theta/arc_segments),DegreesToRadians(360.0))));
    points[2].y=(double) (center.y+sin(fmod((double) (alpha+(double) (i+1)*
      theta/arc_segments),DegreesToRadians(360.0))));
    points[1].x=(double) (points[2].x+gamma*sin(fmod((double) (alpha+(double)
      (i+1)*theta/arc_segments),DegreesToRadians(360.0))));
    points[1].y=(double) (points[2].y-gamma*cos(fmod((double) (alpha+(double)
      (i+1)*theta/arc_segments),DegreesToRadians(360.0))));
    p->point.x=(p == primitive_info) ? start.x : (p-1)->point.x;
    p->point.y=(p == primitive_info) ? start.y : (p-1)->point.y;
    (p+1)->point.x=(double) (cosine*radii.x*points[0].x-sine*radii.y*
      points[0].y);
    (p+1)->point.y=(double) (sine*radii.x*points[0].x+cosine*radii.y*
      points[0].y);
    (p+2)->point.x=(double) (cosine*radii.x*points[1].x-sine*radii.y*
      points[1].y);
    (p+2)->point.y=(double) (sine*radii.x*points[1].x+cosine*radii.y*
      points[1].y);
    (p+3)->point.x=(double) (cosine*radii.x*points[2].x-sine*radii.y*
      points[2].y);
    (p+3)->point.y=(double) (sine*radii.x*points[2].x+cosine*radii.y*
      points[2].y);
    if (i == (ssize_t) (arc_segments-1))
      (p+3)->point=end;
    TraceBezier(p,4);
    p+=p->coordinates;
  }
  primitive_info->coordinates=(size_t) (p-primitive_info);
  primitive_info->closed_subpath=MagickFalse;
  for (i=0; i < (ssize_t) primitive_info->coordinates; i++)
  {
    p->primitive=primitive_info->primitive;
    p--;
  }
}

static void TraceBezier(PrimitiveInfo *primitive_info,
  const size_t number_coordinates)
{
  double
    alpha,
    *coefficients,
    weight;

  PointInfo
    end,
    point,
    *points;

  register PrimitiveInfo
    *p;

  register ssize_t
    i,
    j;

  size_t
    control_points,
    quantum;

  /*
    Allocate coefficients.
  */
  quantum=number_coordinates;
  for (i=0; i < (ssize_t) number_coordinates; i++)
  {
    for (j=i+1; j < (ssize_t) number_coordinates; j++)
    {
      alpha=fabs(primitive_info[j].point.x-primitive_info[i].point.x);
      if (alpha > (double) quantum)
        quantum=(size_t) alpha;
      alpha=fabs(primitive_info[j].point.y-primitive_info[i].point.y);
      if (alpha > (double) quantum)
        quantum=(size_t) alpha;
    }
  }
  quantum=(size_t) MagickMin((double) quantum/number_coordinates,
    (double) BezierQuantum);
  control_points=quantum*number_coordinates;
  coefficients=(double *) AcquireQuantumMemory((size_t)
    number_coordinates,sizeof(*coefficients));
  points=(PointInfo *) AcquireQuantumMemory((size_t) control_points,
    sizeof(*points));
  if ((coefficients == (double *) NULL) || (points == (PointInfo *) NULL))
    ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
  /*
    Compute bezier points.
  */
  end=primitive_info[number_coordinates-1].point;
  for (i=0; i < (ssize_t) number_coordinates; i++)
    coefficients[i]=Permutate((ssize_t) number_coordinates-1,i);
  weight=0.0;
  for (i=0; i < (ssize_t) control_points; i++)
  {
    p=primitive_info;
    point.x=0.0;
    point.y=0.0;
    alpha=pow((double) (1.0-weight),(double) number_coordinates-1.0);
    for (j=0; j < (ssize_t) number_coordinates; j++)
    {
      point.x+=alpha*coefficients[j]*p->point.x;
      point.y+=alpha*coefficients[j]*p->point.y;
      alpha*=weight/(1.0-weight);
      p++;
    }
    points[i]=point;
    weight+=1.0/control_points;
  }
  /*
    Bezier curves are just short segmented polys.
  */
  p=primitive_info;
  for (i=0; i < (ssize_t) control_points; i++)
  {
    TracePoint(p,points[i]);
    p+=p->coordinates;
  }
  TracePoint(p,end);
  p+=p->coordinates;
  primitive_info->coordinates=(size_t) (p-primitive_info);
  primitive_info->closed_subpath=MagickFalse;
  for (i=0; i < (ssize_t) primitive_info->coordinates; i++)
  {
    p->primitive=primitive_info->primitive;
    p--;
  }
  points=(PointInfo *) RelinquishMagickMemory(points);
  coefficients=(double *) RelinquishMagickMemory(coefficients);
}

static void TraceCircle(PrimitiveInfo *primitive_info,const PointInfo start,
  const PointInfo end)
{
  double
    alpha,
    beta,
    radius;

  PointInfo
    offset,
    degrees;

  alpha=end.x-start.x;
  beta=end.y-start.y;
  radius=hypot((double) alpha,(double) beta);
  offset.x=(double) radius;
  offset.y=(double) radius;
  degrees.x=0.0;
  degrees.y=360.0;
  TraceEllipse(primitive_info,start,offset,degrees);
}

static void TraceEllipse(PrimitiveInfo *primitive_info,const PointInfo center,
  const PointInfo radii,const PointInfo arc)
{
  double
    delta,
    step,
    x,
    y;

  PointInfo
    angle,
    point;

  register PrimitiveInfo
    *p;

  register ssize_t
    i;

  /*
    Ellipses are just short segmented polys.
  */
  primitive_info->coordinates=0;
  if ((fabs(radii.x) < DrawEpsilon) || (fabs(radii.y) < DrawEpsilon))
    return;
  delta=2.0*PerceptibleReciprocal(MagickMax(radii.x,radii.y));
  step=MagickPI/8.0;
  if ((delta >= 0.0) && (delta < (MagickPI/8.0)))
    step=MagickPI/(4.0*(MagickPI*PerceptibleReciprocal(delta)/2.0));
  angle.x=DegreesToRadians(arc.x);
  y=arc.y;
  while (y < arc.x)
    y+=360.0;
  angle.y=DegreesToRadians(y);
  for (p=primitive_info; angle.x < angle.y; angle.x+=step)
  {
    point.x=cos(fmod(angle.x,DegreesToRadians(360.0)))*radii.x+center.x;
    point.y=sin(fmod(angle.x,DegreesToRadians(360.0)))*radii.y+center.y;
    TracePoint(p,point);
    p+=p->coordinates;
  }
  point.x=cos(fmod(angle.y,DegreesToRadians(360.0)))*radii.x+center.x;
  point.y=sin(fmod(angle.y,DegreesToRadians(360.0)))*radii.y+center.y;
  TracePoint(p,point);
  p+=p->coordinates;
  primitive_info->coordinates=(size_t) (p-primitive_info);
  primitive_info->closed_subpath=MagickFalse;
  x=fabs(primitive_info[0].point.x-
    primitive_info[primitive_info->coordinates-1].point.x);
  y=fabs(primitive_info[0].point.y-
    primitive_info[primitive_info->coordinates-1].point.y);
  if ((x <= DrawEpsilon) && (y <= DrawEpsilon))
    primitive_info->closed_subpath=MagickTrue;
  for (i=0; i < (ssize_t) primitive_info->coordinates; i++)
  {
    p->primitive=primitive_info->primitive;
    p--;
  }
}

static void TraceLine(PrimitiveInfo *primitive_info,const PointInfo start,
  const PointInfo end)
{
  TracePoint(primitive_info,start);
  if ((fabs(start.x-end.x) < DrawEpsilon) &&
      (fabs(start.y-end.y) < DrawEpsilon))
    {
      primitive_info->primitive=PointPrimitive;
      primitive_info->coordinates=1;
      return;
    }
  TracePoint(primitive_info+1,end);
  (primitive_info+1)->primitive=primitive_info->primitive;
  primitive_info->coordinates=2;
  primitive_info->closed_subpath=MagickFalse;
}

static size_t TracePath(Image *image,PrimitiveInfo *primitive_info,
  const char *path)
{
  char
    *next_token,
    token[MaxTextExtent];

  const char
    *p;

  double
    x,
    y;

  int
    attribute,
    last_attribute;

  MagickBooleanType
    status;

  PointInfo
    end = {0.0, 0.0},
    points[4] = { {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0} },
    point = {0.0, 0.0},
    start = {0.0, 0.0};

  PrimitiveType
    primitive_type;

  register PrimitiveInfo
    *q;

  register ssize_t
    i;

  size_t
    number_coordinates,
    z_count;

  status=MagickTrue;
  attribute=0;
  number_coordinates=0;
  z_count=0;
  primitive_type=primitive_info->primitive;
  q=primitive_info;
  for (p=path; *p != '\0'; )
  {
    if (status == MagickFalse)
      break;
    while (isspace((int) ((unsigned char) *p)) != 0)
      p++;
    if (*p == '\0')
      break;
    last_attribute=attribute;
    attribute=(int) (*p++);
    switch (attribute)
    {
      case 'a':
      case 'A':
      {
        double
          angle = 0.0;

        MagickBooleanType
          large_arc = MagickFalse,
          sweep = MagickFalse;

        PointInfo
          arc = {0.0, 0.0};

        /*
          Elliptical arc.
        */
        do
        {
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          arc.x=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          arc.y=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          angle=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          large_arc=StringToLong(token) != 0 ? MagickTrue : MagickFalse;
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          sweep=StringToLong(token) != 0 ? MagickTrue : MagickFalse;
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          x=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          y=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          end.x=(double) (attribute == (int) 'A' ? x : point.x+x);
          end.y=(double) (attribute == (int) 'A' ? y : point.y+y);
          TraceArcPath(q,point,end,arc,angle,large_arc,sweep);
          q+=q->coordinates;
          point=end;
          while (isspace((int) ((unsigned char) *p)) != 0)
            p++;
          if (*p == ',')
            p++;
        } while (IsPoint(p) != MagickFalse);
        break;
      }
      case 'c':
      case 'C':
      {
        /*
          Cubic Bézier curve.
        */
        do
        {
          points[0]=point;
          for (i=1; i < 4; i++)
          {
            GetNextToken(p,&p,MaxTextExtent,token);
            if (*token == ',')
              GetNextToken(p,&p,MaxTextExtent,token);
            x=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(p,&p,MaxTextExtent,token);
            if (*token == ',')
              GetNextToken(p,&p,MaxTextExtent,token);
            y=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            end.x=(double) (attribute == (int) 'C' ? x : point.x+x);
            end.y=(double) (attribute == (int) 'C' ? y : point.y+y);
            points[i]=end;
          }
          for (i=0; i < 4; i++)
            (q+i)->point=points[i];
          TraceBezier(q,4);
          q+=q->coordinates;
          point=end;
          while (isspace((int) ((unsigned char) *p)) != 0)
            p++;
          if (*p == ',')
            p++;
        } while (IsPoint(p) != MagickFalse);
        break;
      }
      case 'H':
      case 'h':
      {
        do
        {
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          x=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          point.x=(double) (attribute == (int) 'H' ? x: point.x+x);
          TracePoint(q,point);
          q+=q->coordinates;
          while (isspace((int) ((unsigned char) *p)) != 0)
            p++;
          if (*p == ',')
            p++;
        } while (IsPoint(p) != MagickFalse);
        break;
      }
      case 'l':
      case 'L':
      {
        /*
          Line to.
        */
        do
        {
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          x=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          y=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          point.x=(double) (attribute == (int) 'L' ? x : point.x+x);
          point.y=(double) (attribute == (int) 'L' ? y : point.y+y);
          TracePoint(q,point);
          q+=q->coordinates;
          while (isspace((int) ((unsigned char) *p)) != 0)
            p++;
          if (*p == ',')
            p++;
        } while (IsPoint(p) != MagickFalse);
        break;
      }
      case 'M':
      case 'm':
      {
        /*
          Move to.
        */
        if (q != primitive_info)
          {
            primitive_info->coordinates=(size_t) (q-primitive_info);
            number_coordinates+=primitive_info->coordinates;
            primitive_info=q;
          }
        i=0;
        do
        {
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          x=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          y=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          point.x=(double) (attribute == (int) 'M' ? x : point.x+x);
          point.y=(double) (attribute == (int) 'M' ? y : point.y+y);
          if (i == 0)
            start=point;
          i++;
          TracePoint(q,point);
          q+=q->coordinates;
          while (isspace((int) ((unsigned char) *p)) != 0)
            p++;
          if (*p == ',')
            p++;
        } while (IsPoint(p) != MagickFalse);
        break;
      }
      case 'q':
      case 'Q':
      {
        /*
          Quadratic Bézier curve.
        */
        do
        {
          points[0]=point;
          for (i=1; i < 3; i++)
          {
            GetNextToken(p,&p,MaxTextExtent,token);
            if (*token == ',')
              GetNextToken(p,&p,MaxTextExtent,token);
            x=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(p,&p,MaxTextExtent,token);
            if (*token == ',')
              GetNextToken(p,&p,MaxTextExtent,token);
            y=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            if (*p == ',')
              p++;
            end.x=(double) (attribute == (int) 'Q' ? x : point.x+x);
            end.y=(double) (attribute == (int) 'Q' ? y : point.y+y);
            points[i]=end;
          }
          for (i=0; i < 3; i++)
            (q+i)->point=points[i];
          TraceBezier(q,3);
          q+=q->coordinates;
          point=end;
          while (isspace((int) ((unsigned char) *p)) != 0)
            p++;
          if (*p == ',')
            p++;
        } while (IsPoint(p) != MagickFalse);
        break;
      }
      case 's':
      case 'S':
      {
        /*
          Cubic Bézier curve.
        */
        do
        {
          points[0]=points[3];
          points[1].x=2.0*points[3].x-points[2].x;
          points[1].y=2.0*points[3].y-points[2].y;
          for (i=2; i < 4; i++)
          {
            GetNextToken(p,&p,MaxTextExtent,token);
            if (*token == ',')
              GetNextToken(p,&p,MaxTextExtent,token);
            x=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(p,&p,MaxTextExtent,token);
            if (*token == ',')
              GetNextToken(p,&p,MaxTextExtent,token);
            y=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            if (*p == ',')
              p++;
            end.x=(double) (attribute == (int) 'S' ? x : point.x+x);
            end.y=(double) (attribute == (int) 'S' ? y : point.y+y);
            points[i]=end;
          }
          if (strchr("CcSs",last_attribute) == (char *) NULL)
            {
              points[0]=point;
              points[1]=point;
            }
          for (i=0; i < 4; i++)
            (q+i)->point=points[i];
          TraceBezier(q,4);
          q+=q->coordinates;
          point=end;
          last_attribute=attribute;
          while (isspace((int) ((unsigned char) *p)) != 0)
            p++;
          if (*p == ',')
            p++;
        } while (IsPoint(p) != MagickFalse);
        break;
      }
      case 't':
      case 'T':
      {
        /*
          Quadratic Bézier curve.
        */
        do
        {
          points[0]=points[2];
          points[1].x=2.0*points[2].x-points[1].x;
          points[1].y=2.0*points[2].y-points[1].y;
          for (i=2; i < 3; i++)
          {
            GetNextToken(p,&p,MaxTextExtent,token);
            if (*token == ',')
              GetNextToken(p,&p,MaxTextExtent,token);
            x=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            GetNextToken(p,&p,MaxTextExtent,token);
            if (*token == ',')
              GetNextToken(p,&p,MaxTextExtent,token);
            y=StringToDouble(token,&next_token);
            if (token == next_token)
              ThrowPointExpectedException(image,token);
            end.x=(double) (attribute == (int) 'T' ? x : point.x+x);
            end.y=(double) (attribute == (int) 'T' ? y : point.y+y);
            points[i]=end;
          }
          if (status == MagickFalse)
            break;
          if (strchr("QqTt",last_attribute) == (char *) NULL)
            {
              points[0]=point;
              points[1]=point;
            }
          for (i=0; i < 3; i++)
            (q+i)->point=points[i];
          TraceBezier(q,3);
          q+=q->coordinates;
          point=end;
          last_attribute=attribute;
          while (isspace((int) ((unsigned char) *p)) != 0)
            p++;
          if (*p == ',')
            p++;
        } while (IsPoint(p) != MagickFalse);
        break;
      }
      case 'v':
      case 'V':
      {
        /*
          Line to.
        */
        do
        {
          GetNextToken(p,&p,MaxTextExtent,token);
          if (*token == ',')
            GetNextToken(p,&p,MaxTextExtent,token);
          y=StringToDouble(token,&next_token);
          if (token == next_token)
            ThrowPointExpectedException(image,token);
          point.y=(double) (attribute == (int) 'V' ? y : point.y+y);
          TracePoint(q,point);
          q+=q->coordinates;
          while (isspace((int) ((unsigned char) *p)) != 0)
            p++;
          if (*p == ',')
            p++;
        } while (IsPoint(p) != MagickFalse);
        break;
      }
      case 'z':
      case 'Z':
      {
        /*
          Close path.
        */
        point=start;
        TracePoint(q,point);
        q+=q->coordinates;
        primitive_info->coordinates=(size_t) (q-primitive_info);
        primitive_info->closed_subpath=MagickTrue;
        number_coordinates+=primitive_info->coordinates;
        primitive_info=q;
        z_count++;
        break;
      }
      default:
      {
        ThrowPointExpectedException(image,token);
        break;
      }
    }
  }
  if (status == MagickFalse)
    return(0);
  primitive_info->coordinates=(size_t) (q-primitive_info);
  number_coordinates+=primitive_info->coordinates;
  for (i=0; i < (ssize_t) number_coordinates; i++)
  {
    q--;
    q->primitive=primitive_type;
    if (z_count > 1)
      q->method=FillToBorderMethod;
  }
  q=primitive_info;
  return(number_coordinates);
}

static void TraceRectangle(PrimitiveInfo *primitive_info,const PointInfo start,
  const PointInfo end)
{
  PointInfo
    point;

  register PrimitiveInfo
    *p;

  register ssize_t
    i;

  if ((fabs(start.x-end.x) < DrawEpsilon) ||
      (fabs(start.y-end.y) < DrawEpsilon))
    {
      primitive_info->coordinates=0;
      return;
    }
  p=primitive_info;
  TracePoint(p,start);
  p+=p->coordinates;
  point.x=start.x;
  point.y=end.y;
  TracePoint(p,point);
  p+=p->coordinates;
  TracePoint(p,end);
  p+=p->coordinates;
  point.x=end.x;
  point.y=start.y;
  TracePoint(p,point);
  p+=p->coordinates;
  TracePoint(p,start);
  p+=p->coordinates;
  primitive_info->coordinates=(size_t) (p-primitive_info);
  primitive_info->closed_subpath=MagickTrue;
  for (i=0; i < (ssize_t) primitive_info->coordinates; i++)
  {
    p->primitive=primitive_info->primitive;
    p--;
  }
}

static void TraceRoundRectangle(PrimitiveInfo *primitive_info,
  const PointInfo start,const PointInfo end,PointInfo arc)
{
  PointInfo
    degrees,
    offset,
    point;

  register PrimitiveInfo
    *p;

  register ssize_t
    i;

  offset.x=fabs(end.x-start.x);
  offset.y=fabs(end.y-start.y);
  if ((offset.x < DrawEpsilon) || (offset.y < DrawEpsilon))
    {
      primitive_info->coordinates=0;
      return;
    }
  p=primitive_info;
  if (arc.x > (0.5*offset.x))
    arc.x=0.5*offset.x;
  if (arc.y > (0.5*offset.y))
    arc.y=0.5*offset.y;
  point.x=start.x+offset.x-arc.x;
  point.y=start.y+arc.y;
  degrees.x=270.0;
  degrees.y=360.0;
  TraceEllipse(p,point,arc,degrees);
  p+=p->coordinates;
  point.x=start.x+offset.x-arc.x;
  point.y=start.y+offset.y-arc.y;
  degrees.x=0.0;
  degrees.y=90.0;
  TraceEllipse(p,point,arc,degrees);
  p+=p->coordinates;
  point.x=start.x+arc.x;
  point.y=start.y+offset.y-arc.y;
  degrees.x=90.0;
  degrees.y=180.0;
  TraceEllipse(p,point,arc,degrees);
  p+=p->coordinates;
  point.x=start.x+arc.x;
  point.y=start.y+arc.y;
  degrees.x=180.0;
  degrees.y=270.0;
  TraceEllipse(p,point,arc,degrees);
  p+=p->coordinates;
  TracePoint(p,primitive_info->point);
  p+=p->coordinates;
  primitive_info->coordinates=(size_t) (p-primitive_info);
  primitive_info->closed_subpath=MagickTrue;
  for (i=0; i < (ssize_t) primitive_info->coordinates; i++)
  {
    p->primitive=primitive_info->primitive;
    p--;
  }
}

static void TraceSquareLinecap(PrimitiveInfo *primitive_info,
  const size_t number_vertices,const double offset)
{
  double
    distance;

  register double
    dx,
    dy;

  register ssize_t
    i;

  ssize_t
    j;

  dx=0.0;
  dy=0.0;
  for (i=1; i < (ssize_t) number_vertices; i++)
  {
    dx=primitive_info[0].point.x-primitive_info[i].point.x;
    dy=primitive_info[0].point.y-primitive_info[i].point.y;
    if ((fabs((double) dx) >= DrawEpsilon) ||
        (fabs((double) dy) >= DrawEpsilon))
      break;
  }
  if (i == (ssize_t) number_vertices)
    i=(ssize_t) number_vertices-1L;
  distance=hypot((double) dx,(double) dy);
  primitive_info[0].point.x=(double) (primitive_info[i].point.x+
    dx*(distance+offset)/distance);
  primitive_info[0].point.y=(double) (primitive_info[i].point.y+
    dy*(distance+offset)/distance);
  for (j=(ssize_t) number_vertices-2; j >= 0;  j--)
  {
    dx=primitive_info[number_vertices-1].point.x-primitive_info[j].point.x;
    dy=primitive_info[number_vertices-1].point.y-primitive_info[j].point.y;
    if ((fabs((double) dx) >= DrawEpsilon) ||
        (fabs((double) dy) >= DrawEpsilon))
      break;
  }
  distance=hypot((double) dx,(double) dy);
  primitive_info[number_vertices-1].point.x=(double) (primitive_info[j].point.x+
    dx*(distance+offset)/distance);
  primitive_info[number_vertices-1].point.y=(double) (primitive_info[j].point.y+
    dy*(distance+offset)/distance);
}

static PrimitiveInfo *TraceStrokePolygon(const Image *image,
  const DrawInfo *draw_info,const PrimitiveInfo *primitive_info)
{
#define CheckPathExtent(pad) \
  if ((q+(pad)) >= (ssize_t) max_strokes) \
    { \
      if (~max_strokes < (pad)) \
        { \
          path_p=(PointInfo *) RelinquishMagickMemory(path_p); \
          path_q=(PointInfo *) RelinquishMagickMemory(path_q); \
        } \
      else \
        { \
          max_strokes+=(pad); \
          path_p=(PointInfo *) ResizeQuantumMemory(path_p,max_strokes, \
            sizeof(*path_p)); \
          path_q=(PointInfo *) ResizeQuantumMemory(path_q,max_strokes, \
            sizeof(*path_q)); \
        } \
      if ((path_p == (PointInfo *) NULL) || (path_q == (PointInfo *) NULL)) \
        { \
          if (path_p != (PointInfo *) NULL) \
            path_p=(PointInfo *) RelinquishMagickMemory(path_p); \
          if (path_q != (PointInfo *) NULL) \
            path_q=(PointInfo *) RelinquishMagickMemory(path_q); \
          polygon_primitive=(PrimitiveInfo *) \
            RelinquishMagickMemory(polygon_primitive); \
          return((PrimitiveInfo *) NULL); \
        } \
    }

  typedef struct _LineSegment
  {
    double
      p,
      q;
  } LineSegment;

  double
    delta_theta,
    dot_product,
    mid,
    miterlimit;

  LineSegment
    dx = {0,0},
    dy = {0,0},
    inverse_slope = {0,0},
    slope = {0,0},
    theta = {0,0};

  MagickBooleanType
    closed_path;

  PointInfo
    box_p[5],
    box_q[5],
    center,
    offset,
    *path_p,
    *path_q;

  PrimitiveInfo
    *polygon_primitive,
    *stroke_polygon;

  register ssize_t
    i;

  size_t
    arc_segments,
    max_strokes,
    number_vertices;

  ssize_t
    j,
    n,
    p,
    q;

  /*
    Allocate paths.
  */
  number_vertices=primitive_info->coordinates;
  max_strokes=2*number_vertices+6*BezierQuantum+360;
  polygon_primitive=(PrimitiveInfo *) AcquireQuantumMemory((size_t)
    number_vertices+2UL,sizeof(*polygon_primitive));
  if (polygon_primitive == (PrimitiveInfo *) NULL)
    return((PrimitiveInfo *) NULL);
  (void) memcpy(polygon_primitive,primitive_info,(size_t) number_vertices*
    sizeof(*polygon_primitive));
  closed_path=primitive_info[0].closed_subpath;
  if (((draw_info->linejoin == RoundJoin) ||
       (draw_info->linejoin == MiterJoin)) && (closed_path != MagickFalse))
    {
      polygon_primitive[number_vertices]=primitive_info[1];
      number_vertices++;
    }
  polygon_primitive[number_vertices].primitive=UndefinedPrimitive;
  /*
    Compute the slope for the first line segment, p.
  */
  dx.p=0.0;
  dy.p=0.0;
  for (n=1; n < (ssize_t) number_vertices; n++)
  {
    dx.p=polygon_primitive[n].point.x-polygon_primitive[0].point.x;
    dy.p=polygon_primitive[n].point.y-polygon_primitive[0].point.y;
    if ((fabs(dx.p) >= DrawEpsilon) || (fabs(dy.p) >= DrawEpsilon))
      break;
  }
  if (n == (ssize_t) number_vertices)
    {
      if ((draw_info->linecap != RoundCap) || (closed_path != MagickFalse))
        {
          /*
            Zero length subpath.
          */
          stroke_polygon=(PrimitiveInfo *) AcquireCriticalMemory(
            sizeof(*stroke_polygon));
          stroke_polygon[0]=polygon_primitive[0];
          stroke_polygon[0].coordinates=0;
          polygon_primitive=(PrimitiveInfo *) RelinquishMagickMemory(
            polygon_primitive);
          return(stroke_polygon);
        }
      n=(ssize_t) number_vertices-1L;
    }
  path_p=(PointInfo *) AcquireQuantumMemory((size_t) max_strokes,
    sizeof(*path_p));
  if (path_p == (PointInfo *) NULL)
    {
      polygon_primitive=(PrimitiveInfo *) RelinquishMagickMemory(
        polygon_primitive);
      return((PrimitiveInfo *) NULL);
    }
  path_q=(PointInfo *) AcquireQuantumMemory((size_t) max_strokes,
    sizeof(*path_q));
  if (path_q == (PointInfo *) NULL)
    {
      path_p=(PointInfo *) RelinquishMagickMemory(path_p);
      polygon_primitive=(PrimitiveInfo *) RelinquishMagickMemory(
        polygon_primitive);
      return((PrimitiveInfo *) NULL);
    }
  slope.p=0.0;
  inverse_slope.p=0.0;
  if (fabs(dx.p) < DrawEpsilon)
    {
      if (dx.p >= 0.0)
        slope.p=dy.p < 0.0 ? -1.0/DrawEpsilon : 1.0/DrawEpsilon;
      else
        slope.p=dy.p < 0.0 ? 1.0/DrawEpsilon : -1.0/DrawEpsilon;
    }
  else
    if (fabs(dy.p) < DrawEpsilon)
      {
        if (dy.p >= 0.0)
          inverse_slope.p=dx.p < 0.0 ? -1.0/DrawEpsilon : 1.0/DrawEpsilon;
        else
          inverse_slope.p=dx.p < 0.0 ? 1.0/DrawEpsilon : -1.0/DrawEpsilon;
      }
    else
      {
        slope.p=dy.p/dx.p;
        inverse_slope.p=(-1.0/slope.p);
      }
  mid=ExpandAffine(&draw_info->affine)*SaneStrokeWidth(image,draw_info)/2.0;
  miterlimit=(double) (draw_info->miterlimit*draw_info->miterlimit*mid*mid);
  if ((draw_info->linecap == SquareCap) && (closed_path == MagickFalse))
    TraceSquareLinecap(polygon_primitive,number_vertices,mid);
  offset.x=sqrt((double) (mid*mid/(inverse_slope.p*inverse_slope.p+1.0)));
  offset.y=(double) (offset.x*inverse_slope.p);
  if ((dy.p*offset.x-dx.p*offset.y) > 0.0)
    {
      box_p[0].x=polygon_primitive[0].point.x-offset.x;
      box_p[0].y=polygon_primitive[0].point.y-offset.x*inverse_slope.p;
      box_p[1].x=polygon_primitive[n].point.x-offset.x;
      box_p[1].y=polygon_primitive[n].point.y-offset.x*inverse_slope.p;
      box_q[0].x=polygon_primitive[0].point.x+offset.x;
      box_q[0].y=polygon_primitive[0].point.y+offset.x*inverse_slope.p;
      box_q[1].x=polygon_primitive[n].point.x+offset.x;
      box_q[1].y=polygon_primitive[n].point.y+offset.x*inverse_slope.p;
    }
  else
    {
      box_p[0].x=polygon_primitive[0].point.x+offset.x;
      box_p[0].y=polygon_primitive[0].point.y+offset.y;
      box_p[1].x=polygon_primitive[n].point.x+offset.x;
      box_p[1].y=polygon_primitive[n].point.y+offset.y;
      box_q[0].x=polygon_primitive[0].point.x-offset.x;
      box_q[0].y=polygon_primitive[0].point.y-offset.y;
      box_q[1].x=polygon_primitive[n].point.x-offset.x;
      box_q[1].y=polygon_primitive[n].point.y-offset.y;
    }
  /*
    Create strokes for the line join attribute: bevel, miter, round.
  */
  p=0;
  q=0;
  path_q[p++]=box_q[0];
  path_p[q++]=box_p[0];
  for (i=(ssize_t) n+1; i < (ssize_t) number_vertices; i++)
  {
    /*
      Compute the slope for this line segment, q.
    */
    dx.q=polygon_primitive[i].point.x-polygon_primitive[n].point.x;
    dy.q=polygon_primitive[i].point.y-polygon_primitive[n].point.y;
    dot_product=dx.q*dx.q+dy.q*dy.q;
    if (dot_product < 0.25)
      continue;
    slope.q=0.0;
    inverse_slope.q=0.0;
    if (fabs(dx.q) < DrawEpsilon)
      {
        if (dx.q >= 0.0)
          slope.q=dy.q < 0.0 ? -1.0/DrawEpsilon : 1.0/DrawEpsilon;
        else
          slope.q=dy.q < 0.0 ? 1.0/DrawEpsilon : -1.0/DrawEpsilon;
      }
    else
      if (fabs(dy.q) < DrawEpsilon)
        {
          if (dy.q >= 0.0)
            inverse_slope.q=dx.q < 0.0 ? -1.0/DrawEpsilon : 1.0/DrawEpsilon;
          else
            inverse_slope.q=dx.q < 0.0 ? 1.0/DrawEpsilon : -1.0/DrawEpsilon;
        }
      else
        {
          slope.q=dy.q/dx.q;
          inverse_slope.q=(-1.0/slope.q);
        }
    offset.x=sqrt((double) (mid*mid/(inverse_slope.q*inverse_slope.q+1.0)));
    offset.y=(double) (offset.x*inverse_slope.q);
    dot_product=dy.q*offset.x-dx.q*offset.y;
    if (dot_product > 0.0)
      {
        box_p[2].x=polygon_primitive[n].point.x-offset.x;
        box_p[2].y=polygon_primitive[n].point.y-offset.y;
        box_p[3].x=polygon_primitive[i].point.x-offset.x;
        box_p[3].y=polygon_primitive[i].point.y-offset.y;
        box_q[2].x=polygon_primitive[n].point.x+offset.x;
        box_q[2].y=polygon_primitive[n].point.y+offset.y;
        box_q[3].x=polygon_primitive[i].point.x+offset.x;
        box_q[3].y=polygon_primitive[i].point.y+offset.y;
      }
    else
      {
        box_p[2].x=polygon_primitive[n].point.x+offset.x;
        box_p[2].y=polygon_primitive[n].point.y+offset.y;
        box_p[3].x=polygon_primitive[i].point.x+offset.x;
        box_p[3].y=polygon_primitive[i].point.y+offset.y;
        box_q[2].x=polygon_primitive[n].point.x-offset.x;
        box_q[2].y=polygon_primitive[n].point.y-offset.y;
        box_q[3].x=polygon_primitive[i].point.x-offset.x;
        box_q[3].y=polygon_primitive[i].point.y-offset.y;
      }
    if (fabs((double) (slope.p-slope.q)) < DrawEpsilon)
      {
        box_p[4]=box_p[1];
        box_q[4]=box_q[1];
      }
    else
      {
        box_p[4].x=(double) ((slope.p*box_p[0].x-box_p[0].y-slope.q*box_p[3].x+
          box_p[3].y)/(slope.p-slope.q));
        box_p[4].y=(double) (slope.p*(box_p[4].x-box_p[0].x)+box_p[0].y);
        box_q[4].x=(double) ((slope.p*box_q[0].x-box_q[0].y-slope.q*box_q[3].x+
          box_q[3].y)/(slope.p-slope.q));
        box_q[4].y=(double) (slope.p*(box_q[4].x-box_q[0].x)+box_q[0].y);
      }
    CheckPathExtent(6*BezierQuantum+360);
    dot_product=dx.q*dy.p-dx.p*dy.q;
    if (dot_product <= 0.0)
      switch (draw_info->linejoin)
      {
        case BevelJoin:
        {
          path_q[q++]=box_q[1];
          path_q[q++]=box_q[2];
          dot_product=(box_q[4].x-box_p[4].x)*(box_q[4].x-box_p[4].x)+
            (box_q[4].y-box_p[4].y)*(box_q[4].y-box_p[4].y);
          if (dot_product <= miterlimit)
            path_p[p++]=box_p[4];
          else
            {
              path_p[p++]=box_p[1];
              path_p[p++]=box_p[2];
            }
          break;
        }
        case MiterJoin:
        {
          dot_product=(box_q[4].x-box_p[4].x)*(box_q[4].x-box_p[4].x)+
            (box_q[4].y-box_p[4].y)*(box_q[4].y-box_p[4].y);
          if (dot_product <= miterlimit)
            {
              path_q[q++]=box_q[4];
              path_p[p++]=box_p[4];
            }
          else
            {
              path_q[q++]=box_q[1];
              path_q[q++]=box_q[2];
              path_p[p++]=box_p[1];
              path_p[p++]=box_p[2];
            }
          break;
        }
        case RoundJoin:
        {
          dot_product=(box_q[4].x-box_p[4].x)*(box_q[4].x-box_p[4].x)+
            (box_q[4].y-box_p[4].y)*(box_q[4].y-box_p[4].y);
          if (dot_product <= miterlimit)
            path_p[p++]=box_p[4];
          else
            {
              path_p[p++]=box_p[1];
              path_p[p++]=box_p[2];
            }
          center=polygon_primitive[n].point;
          theta.p=atan2(box_q[1].y-center.y,box_q[1].x-center.x);
          theta.q=atan2(box_q[2].y-center.y,box_q[2].x-center.x);
          if (theta.q < theta.p)
            theta.q+=2.0*MagickPI;
          arc_segments=(size_t) ceil((double) ((theta.q-theta.p)/
            (2.0*sqrt((double) (1.0/mid)))));
          CheckPathExtent(arc_segments+6*BezierQuantum+360);
          path_q[q].x=box_q[1].x;
          path_q[q].y=box_q[1].y;
          q++;
          for (j=1; j < (ssize_t) arc_segments; j++)
          {
            delta_theta=(double) (j*(theta.q-theta.p)/arc_segments);
            path_q[q].x=(double) (center.x+mid*cos(fmod((double)
              (theta.p+delta_theta),DegreesToRadians(360.0))));
            path_q[q].y=(double) (center.y+mid*sin(fmod((double)
              (theta.p+delta_theta),DegreesToRadians(360.0))));
            q++;
          }
          path_q[q++]=box_q[2];
          break;
        }
        default:
          break;
      }
    else
      switch (draw_info->linejoin)
      {
        case BevelJoin:
        {
          path_p[p++]=box_p[1];
          path_p[p++]=box_p[2];
          dot_product=(box_q[4].x-box_p[4].x)*(box_q[4].x-box_p[4].x)+
            (box_q[4].y-box_p[4].y)*(box_q[4].y-box_p[4].y);
          if (dot_product <= miterlimit)
            path_q[q++]=box_q[4];
          else
            {
              path_q[q++]=box_q[1];
              path_q[q++]=box_q[2];
            }
          break;
        }
        case MiterJoin:
        {
          dot_product=(box_q[4].x-box_p[4].x)*(box_q[4].x-box_p[4].x)+
            (box_q[4].y-box_p[4].y)*(box_q[4].y-box_p[4].y);
          if (dot_product <= miterlimit)
            {
              path_q[q++]=box_q[4];
              path_p[p++]=box_p[4];
            }
          else
            {
              path_q[q++]=box_q[1];
              path_q[q++]=box_q[2];
              path_p[p++]=box_p[1];
              path_p[p++]=box_p[2];
            }
          break;
        }
        case RoundJoin:
        {
          dot_product=(box_q[4].x-box_p[4].x)*(box_q[4].x-box_p[4].x)+
            (box_q[4].y-box_p[4].y)*(box_q[4].y-box_p[4].y);
          if (dot_product <= miterlimit)
            path_q[q++]=box_q[4];
          else
            {
              path_q[q++]=box_q[1];
              path_q[q++]=box_q[2];
            }
          center=polygon_primitive[n].point;
          theta.p=atan2(box_p[1].y-center.y,box_p[1].x-center.x);
          theta.q=atan2(box_p[2].y-center.y,box_p[2].x-center.x);
          if (theta.p < theta.q)
            theta.p+=2.0*MagickPI;
          arc_segments=(size_t) ceil((double) ((theta.p-theta.q)/
            (2.0*sqrt((double) (1.0/mid)))));
          CheckPathExtent(arc_segments+6*BezierQuantum+360);
          path_p[p++]=box_p[1];
          for (j=1; j < (ssize_t) arc_segments; j++)
          {
            delta_theta=(double) (j*(theta.q-theta.p)/arc_segments);
            path_p[p].x=(double) (center.x+mid*cos(fmod((double)
              (theta.p+delta_theta),DegreesToRadians(360.0))));
            path_p[p].y=(double) (center.y+mid*sin(fmod((double)
              (theta.p+delta_theta),DegreesToRadians(360.0))));
            p++;
          }
          path_p[p++]=box_p[2];
          break;
        }
        default:
          break;
      }
    slope.p=slope.q;
    inverse_slope.p=inverse_slope.q;
    box_p[0]=box_p[2];
    box_p[1]=box_p[3];
    box_q[0]=box_q[2];
    box_q[1]=box_q[3];
    dx.p=dx.q;
    dy.p=dy.q;
    n=i;
  }
  path_p[p++]=box_p[1];
  path_q[q++]=box_q[1];
  /*
    Trace stroked polygon.
  */
  stroke_polygon=(PrimitiveInfo *) AcquireQuantumMemory((size_t)
    (p+q+2UL*closed_path+2UL),sizeof(*stroke_polygon));
  if (stroke_polygon != (PrimitiveInfo *) NULL)
    {
      for (i=0; i < (ssize_t) p; i++)
      {
        stroke_polygon[i]=polygon_primitive[0];
        stroke_polygon[i].point=path_p[i];
      }
      if (closed_path != MagickFalse)
        {
          stroke_polygon[i]=polygon_primitive[0];
          stroke_polygon[i].point=stroke_polygon[0].point;
          i++;
        }
      for ( ; i < (ssize_t) (p+q+closed_path); i++)
      {
        stroke_polygon[i]=polygon_primitive[0];
        stroke_polygon[i].point=path_q[p+q+closed_path-(i+1)];
      }
      if (closed_path != MagickFalse)
        {
          stroke_polygon[i]=polygon_primitive[0];
          stroke_polygon[i].point=stroke_polygon[p+closed_path].point;
          i++;
        }
      stroke_polygon[i]=polygon_primitive[0];
      stroke_polygon[i].point=stroke_polygon[0].point;
      i++;
      stroke_polygon[i].primitive=UndefinedPrimitive;
      stroke_polygon[0].coordinates=(size_t) (p+q+2*closed_path+1);
    }
  path_p=(PointInfo *) RelinquishMagickMemory(path_p);
  path_q=(PointInfo *) RelinquishMagickMemory(path_q);
  polygon_primitive=(PrimitiveInfo *) RelinquishMagickMemory(polygon_primitive);
  return(stroke_polygon);
}
