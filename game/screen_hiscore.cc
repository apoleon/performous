#include "screen_hiscore.hh"

#include "configuration.hh"
#include "audio.hh"
#include "fs.hh"
#include "util.hh"
#include "database.hh"

#include <iostream>
#include <sstream>
#include <boost/format.hpp>

static const double IDLE_TIMEOUT = 45.0; // seconds

ScreenHiscore::ScreenHiscore(std::string const& name, Audio& audio, Database& database):
  Screen(name), m_audio(audio), m_database(database), m_players(database.m_players), m_covers(20)
{
	m_players.setAnimMargins(5.0, 5.0);
	m_playTimer.setTarget(getInf()); // Using this as a simple timer counting seconds
}

void ScreenHiscore::enter() {
	if (!m_song->background.empty()) {
		try {
			m_background.reset(new Surface(m_song->path + m_song->background, true));
		} catch (std::exception& e) {
			std::cerr << e.what() << std::endl;
		}
	}

	if (!m_song->video.empty() && config["graphic/video"].b()) {
		m_video.reset(new Video(m_song->path + m_song->video, m_song->videoGap));
	}

	m_score_text[0].reset(new SvgTxtThemeSimple(getThemePath("sing_score_text.svg"), config["graphic/text_lod"].f()));
	m_score_text[1].reset(new SvgTxtThemeSimple(getThemePath("sing_score_text.svg"), config["graphic/text_lod"].f()));
	m_score_text[2].reset(new SvgTxtThemeSimple(getThemePath("sing_score_text.svg"), config["graphic/text_lod"].f()));
	m_score_text[3].reset(new SvgTxtThemeSimple(getThemePath("sing_score_text.svg"), config["graphic/text_lod"].f()));
	m_player_icon.reset(new Surface(getThemePath("sing_pbox.svg")));
	theme.reset(new ThemeSongs());
	m_emptyCover.reset(new Surface(getThemePath("no_cover.svg"))); // TODO use persons head
	m_search.text.clear();
	m_players.setFilter(m_search.text);
	// m_audio.fadeout();
	m_audio.playMusic(m_song->music, false, 0.0, -1.0);
}

void ScreenHiscore::exit() {
	m_player_icon.reset();
	m_score_text[0].reset();
	m_score_text[1].reset();
	m_score_text[2].reset();
	m_score_text[3].reset();

	m_covers.clear();
	m_emptyCover.reset();
	theme.reset();
	m_video.reset();
	m_songbg.reset();
	m_playing.clear();
	m_playReq.clear();
}

void ScreenHiscore::activateNextScreen() {
	ScreenManager* sm = ScreenManager::getSingletonPtr();
	sm->activateScreen("Songs");
}

void ScreenHiscore::manageEvent(SDL_Event event) {
	if (event.type != SDL_KEYDOWN) return;
	SDL_keysym keysym = event.key.keysym;
	int key = keysym.sym;
	SDLMod mod = event.key.keysym.mod;

	// TODO: reload button? - needs database reload
	// if (key == SDLK_r && mod & KMOD_CTRL) { m_players.reload(); m_players.setFilter(m_search.text); }
	if (m_search.process(keysym)) m_players.setFilter(m_search.text);
	else if (key == SDLK_ESCAPE) {
		if (m_search.text.empty()) { activateNextScreen(); return; }
	else { m_search.text.clear(); m_players.setFilter(m_search.text); }
	}
	// The rest are only available when there are songs available
	else if (m_players.empty()) return;
	else if (key == SDLK_SPACE || (key == SDLK_PAUSE || (key == SDLK_p && mod & KMOD_CTRL))) m_audio.togglePause();
	else if (key == SDLK_RETURN) { activateNextScreen(); return; }
	else if (key == SDLK_LEFT) m_players.advance(-1);
	else if (key == SDLK_RIGHT) m_players.advance(1);
	else if (key == SDLK_PAGEUP) m_players.advance(-10);
	else if (key == SDLK_PAGEDOWN) m_players.advance(10);
}

/**Draw the scores in the bottom*/
void ScreenHiscore::drawScores() {
	// Score display
	{
		unsigned int i = 0;
		for (std::list<Player>::const_iterator p = m_database.cur.begin(); p != m_database.cur.end(); ++p, ++i) {
			float act = p->activity();
			if (act == 0.0f) continue;
			glColor4f(p->m_color.r, p->m_color.g, p->m_color.b,act);
			m_player_icon->dimensions.left(-0.5 + 0.01 + 0.25 * i).fixedWidth(0.075)
				.screenBottom(-0.025);
			m_player_icon->draw();
			m_score_text[i%4]->render((boost::format("%04d") % p->getScore()).str());
			m_score_text[i%4]->dimensions().middle(-0.350 + 0.01 + 0.25 * i).fixedHeight(0.075)
				.screenBottom(-0.025);
			m_score_text[i%4]->draw();
			glColor4f(1.0, 1.0, 1.0, 1.0);
		}
	}
}

namespace {

	const double arMin = 1.33;
	const double arMax = 2.35;

	void fillBG() {
		Dimensions dim(arMin);
		dim.fixedWidth(1.0);
		glutil::Begin block(GL_QUADS);
		glVertex2f(dim.x1(), dim.y1());
		glVertex2f(dim.x2(), dim.y1());
		glVertex2f(dim.x2(), dim.y2());
		glVertex2f(dim.x1(), dim.y2());
	}

}

void ScreenHiscore::draw() {
	const double arMin = 1.33;
	const double arMax = 2.35;

	m_players.update(); // Poll for new players
	double length = m_audio.getLength();
	double time = clamp(m_audio.getPosition() - config["audio/video_delay"].f(), 0.0, length);
	time -= config["audio/video_delay"].f();
	
	// Rendering starts
	{
		double ar = arMax;
		if (m_background) {
			ar = m_background->dimensions.ar();
			if (ar > arMax || (m_video && ar > arMin)) fillBG();  // Fill white background to avoid black borders
			m_background->draw();
		} else fillBG();
		if (m_video) { m_video->render(time); double tmp = m_video->dimensions().ar(); if (tmp > 0.0) ar = tmp; }
		ar = clamp(ar, arMin, arMax);
	}

	theme->bg.draw();
	std::string music, songbg, video;
	double videoGap = 0.0;
	std::ostringstream oss_song, oss_order;

	// Format the player information text
	oss_song << "Hiscore for " << m_song->title << "!\n";

	m_database.queryPerSongHiscore(oss_order, m_song);

	// Draw song and order texts
	theme->song.draw(oss_song.str());
	theme->order.draw(oss_order.str());

		// Schedule playback change if the chosen song has changed
	if (music != m_playReq) { m_playReq = music; m_playTimer.setValue(0.0); }
	// Play/stop preview playback (if it is the time)
	if (music != m_playing && m_playTimer.get() > 0.4) {
		m_songbg.reset(); m_video.reset();
		// if (music.empty()) m_audio.fadeout(); else m_audio.playPreview(music, 30.0);
		if (!songbg.empty()) try { m_songbg.reset(new Surface(songbg)); } catch (std::exception const&) {}
		if (!video.empty() && config["graphic/video"].b()) m_video.reset(new Video(video, videoGap));
		m_playing = music;
	} else if (!m_audio.isPaused() && m_playTimer.get() > IDLE_TIMEOUT) {  // Switch if song hasn't changed for IDLE_TIMEOUT seconds
		if (!m_search.text.empty()) { m_search.text.clear(); m_players.setFilter(m_search.text); }
	}
}
