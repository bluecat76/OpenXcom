/*
 * Copyright 2010-2012 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef OPENXCOM_PURCHASEERRORSTATE_H
#define OPENXCOM_PURCHASEERRORSTATE_H

#include "../Engine/State.h"
#include <string>

namespace OpenXcom
{

class TextButton;
class Window;
class Text;

/**
 * Generic window used to display error messages
 * when the player is on the Purchase screen.
 */
class PurchaseErrorState : public State
{
private:
	TextButton *_btnOk;
	Window *_window;
	Text *_txtError;
public:
	/// Creates the Purchase Error state.
	PurchaseErrorState(Game *game, std::string str);
	/// Cleans up the Purchase Error state.
	~PurchaseErrorState();
	/// Handler for clicking the OK button.
	void btnOkClick(Action *action);
};

}

#endif
