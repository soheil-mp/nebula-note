#pragma once

#include <QString>

// Video page links (YouTube, Vimeo) and the players that embed them.
namespace MediaLinks
{
// True for a link to a single YouTube or Vimeo video.
bool isVideoLink(const QString &url);
// The YouTube video id in a watch, youtu.be, shorts, live or embed link.
QString youtubeId(const QString &url);
// The embeddable player for a video link (empty when it isn't one).
QString playerUrl(const QString &url, bool autoplay = true);
// Where a video's title and thumbnail can be asked for (YouTube's oEmbed).
QString oembedUrl(const QString &url);
// A ready thumbnail image for a video link, when one is known without asking.
QString thumbnailUrl(const QString &url);
} // namespace MediaLinks
