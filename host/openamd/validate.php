<?php

/***************************************************************
 *
 * amd.hope.net - index.php
 *
 * Copyright 2008 The OpenAMD Project <contribute@openamd.org>
 *
/***************************************************************

/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/



/* Activate accounts
    Parameters: userid, type
        type:     0 - email
            1 - sms */

if (!isset($_REQUEST['id'])) {
    set_error("Incorrect parameters for activation.<br>", $_REQUEST['id']);
}

$email = false;
$phone = false;

if (isset($_REQUEST['email'])) {
    // make sure it is not already activated for the account
