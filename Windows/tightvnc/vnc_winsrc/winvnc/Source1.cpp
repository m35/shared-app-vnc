SharedAppVnc::SendUpdates()
{
	vncRegion toBeSent;			// Region to actually be sent
	rectlist toBeSentList;		// List of rectangles to actually send
	vncRegion toBeDone;			// Region to check
	vncRegion winRegion;
	vncWinList::iterator winIter;

	// Prepare to send cursor position update if necessary
	if (m_cursor_pos_changed) {
		POINT cursor_pos;
		if (!GetCursorPos(&cursor_pos)) {
			cursor_pos.x = 0;
			cursor_pos.y = 0;
		}
		if (cursor_pos.x == m_cursor_pos.x && cursor_pos.y == m_cursor_pos.y) {
			m_cursor_pos_changed = FALSE;
		} else {
			m_cursor_pos.x = cursor_pos.x;
			m_cursor_pos.y = cursor_pos.y;
		}
	}

	// If there is nothing to send then exit
	if (m_changed_rgn.IsEmpty() &&
		m_full_rgn.IsEmpty() &&
		!m_copyrect_set &&
		!m_cursor_update_pending &&
		!m_cursor_pos_changed)
		return FALSE;

	// We currently don't handle copyrect with shared app
	// So combine copyrect area into changed region.
	m_changed_rgn.AddRect(m_copyrect_rect);
	m_copyrect_set = FALSE;


	// GRAB THE SCREEN DATA

	// Get the region to be scanned and potentially sent
	toBeDone.Clear();
	toBeDone.Combine(m_incr_rgn);
	toBeDone.Subtract(m_full_rgn);
	toBeDone.Intersect(m_changed_rgn);

	// Get the region to grab
	vncRegion toBeGrabbed;
	toBeGrabbed.Clear();
	toBeGrabbed.Combine(m_full_rgn);
	toBeGrabbed.Combine(toBeDone);
	GrabRegion(toBeGrabbed);

	// CLEAR REGIONS THAT WON'T BE SCANNED

	// Get the region to definitely be sent
	toBeSent.Clear();
	if (!m_full_rgn.IsEmpty())
	{
		rectlist rectsToClear;

		// Retrieve and clear the rectangles
		if (m_full_rgn.Rectangles(rectsToClear))
			ClearRects(toBeSent, rectsToClear);
	}

	// SCAN INCREMENTAL REGIONS FOR CHANGES

	if (!toBeDone.IsEmpty())
	{
		rectlist rectsToScan;

		// Retrieve and scan the rectangles
		if (toBeDone.Rectangles(rectsToScan))
			CheckRects(toBeSent, rectsToScan);
	}

	// CLEAN UP THE MAIN REGIONS

	// Clear the bits we're about to deal with from the changed region
	m_changed_rgn.Subtract(m_incr_rgn);
	m_changed_rgn.Subtract(m_full_rgn);

	// Clear the full & incremental regions, since we've dealt with them
	if (!toBeSent.IsEmpty())
	{
		m_full_rgn.Clear();
		m_incr_rgn.Clear();
	}


	// Loop through shared windows sending updates
	for (winIter = m_shapp->sharedAppList.begin(); winIter != m_unauthClients.end(); winIter++)
	{
		HWND winHwnd = *winIter;
		RECT winRect;

		if (!isWindow(winHwnd))
		{
			removeWindow(winHwnd);
		}

		GetWindowRect(winHwnd, &winRect);
		winRegion.AddRect(winRect);
		winRegion.Intersect(toBeSent);

		if (!m_cursor_update_sent && !m_cursor_update_pending) {
			if (!m_mousemoved) {
				vncRegion tmpMouseRgn;
				tmpMouseRgn.AddRect(m_oldmousepos);
				tmpMouseRgn.Intersect(toBeSent);
				if (!tmpMouseRgn.IsEmpty()) {
					m_mousemoved = true;
				}
			}
			if (m_mousemoved)
			{
				// Grab the mouse
				m_oldmousepos = m_buffer->GrabMouse();
				if (IntersectRect(&m_oldmousepos, &m_oldmousepos, &m_fullscreen))
					m_buffer->GetChangedRegion(toBeSent, m_oldmousepos);

				m_mousemoved = FALSE;
			}
		}



		// Get the list of changed rectangles!
		int numrects = 0;
		if (winRegion.Rectangles(toBeSentList))
		{
			// Find out how many rectangles this update will contain
			rectlist::iterator i;
			int numsubrects;
			for (i=toBeSentList.begin(); i != toBeSentList.end(); i++)
			{
				numsubrects = m_buffer->GetNumCodedRects(*i);

				// Skip rest rectangles if an encoder will use LastRect extension.
				if (numsubrects == 0) {
					numrects = 0xFFFF;
					break;
				}
				numrects += numsubrects;
			}
		}

		if (numrects != 0xFFFF) {
			// Count cursor shape and cursor position updates.
			if (m_cursor_update_pending)
				numrects++;
			if (m_cursor_pos_changed)
				numrects++;
			// Count the copyrect region
			if (m_copyrect_set)
				numrects++;
			// If there are no rectangles then return
			if (numrects == 0)
				return FALSE;
		}

		// Otherwise, send <number of rectangles> header
		rfbSharedAppUpdateMsg header;
		header.win_id = Swap32IfLE(winHwnd);
        header.parent_id = Swap32IfLE(NULL);
        header.win_rect.x = Swap16IfLE(winRect.left);
        header.win_rect.y = Swap16IfLE(winRect.top);
        header.win_rect.w = Swap16IfLE(winRect.right-winRect.left);
        header.win_rect.h = Swap16IfLE(winRect.bottom-winRect.top);
		header.nRects = Swap16IfLE(numrects);
		if (!SendRFBMsg(rfbSharedAppUpdate, (BYTE *) &header, sz_rfbSharedAppUpdateMsg))
			return TRUE;

		// Send mouse cursor shape update
		if (m_cursor_update_pending) {
			if (!SendCursorShapeUpdate())
				return TRUE;
		}

		// Send cursor position update
		if (m_cursor_pos_changed) {
			if (!SendCursorPosUpdate())
				return TRUE;
		}

		// Encode & send the copyrect
		if (m_copyrect_set) {
			m_copyrect_set = FALSE;
			if(!SendCopyRect(m_copyrect_rect, m_copyrect_src))
				return TRUE;
		}

		// Encode & send the actual rectangles
		if (!SendRectangles(toBeSentList))
			return TRUE;

		// Send LastRect marker if needed.
		if (numrects == 0xFFFF) {
			if (!SendLastRect())
				return TRUE;
		}

		// Both lists should be empty when we're done
		_ASSERT(toBeSentList.empty());
	}

	return TRUE;
}

